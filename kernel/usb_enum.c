// SPDX-License-Identifier: GPL-2.0+
/*
Basado en la enumeración USB, usb_hub.c y xHCI de U-Boot.
Copyright (C) 2008 Intel Corp.
Copyright (C) 2013 Samsung Electronics Co.Ltd
(C) Copyright 2001 Denis Peter, MPL AG Switzerland.
*/

/*
usb_enum.c

Enumeración limitada a la topología real de la VisionFive 2:

VL805 xHCI
  -> hub USB 2.0 interno VIA 2109:3431
     -> un teclado USB HID Boot

No se implementan hubs externos, más de un nivel de hub, hotplug ni una
enumeración USB general.
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "usb_xhci.h"

struct usb_open_device {
  uint32 slot_id;
  uint32 speed;
  uchar configuration;
  uint16 config_length;
  struct usb_device_descriptor descriptor;
  uchar *config_buffer;
};

static uint16
usb_get_le16(const void *pointer)
{
  const uchar *bytes;

  bytes = pointer;
  return (uint16)bytes[0] | ((uint16)bytes[1] << 8);
}

static int
usb_get_descriptor(uint32 slot_id,
                   uchar type,
                   uchar index,
                   void *buffer,
                   uint16 length)
{
  return xhci_control_transfer(slot_id,
                               USB_DIR_IN |
                               USB_TYPE_STANDARD |
                               USB_RECIP_DEVICE,
                               USB_REQ_GET_DESCRIPTOR,
                               ((uint16)type << 8) | index,
                               0,
                               buffer,
                               length);
}

static int
usb_set_configuration(uint32 slot_id, uchar configuration)
{
  return xhci_control_transfer(slot_id,
                               USB_TYPE_STANDARD |
                               USB_RECIP_DEVICE,
                               USB_REQ_SET_CONFIGURATION,
                               configuration,
                               0,
                               0,
                               0);
}

static int
usb_hid_set_protocol(uint32 slot_id, uchar interface_number)
{
  return xhci_control_transfer(slot_id,
                               USB_TYPE_CLASS |
                               USB_RECIP_INTERFACE,
                               USB_REQ_SET_PROTOCOL,
                               0,
                               interface_number,
                               0,
                               0);
}

static int
usb_hid_set_idle(uint32 slot_id, uchar interface_number)
{
  return xhci_control_transfer(slot_id,
                               USB_TYPE_CLASS |
                               USB_RECIP_INTERFACE,
                               USB_REQ_SET_IDLE,
                               0,
                               interface_number,
                               0,
                               0);
}

static uint32
usb_ep0_packet_size(uint32 speed, uchar descriptor_value)
{
  if(speed == XHCI_SPEED_SUPER){
    if(descriptor_value > 15U)
      return 0;
    return 1U << descriptor_value;
  }

  return descriptor_value;
}

static int
usb_update_ep0_packet(uint32 slot_id, uint32 packet)
{
  struct xhci_device *device;
  uint32 *control;
  uint32 *slot_in;
  uint32 *ep0_in;
  uint32 *slot_out;
  uint32 *ep0_out;
  uint32 input_size;
  uint32 output_size;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  if(packet == device->ep0_max_packet)
    return 0;

  if(packet != 8U && packet != 16U &&
     packet != 32U && packet != 64U && packet != 512U){
    printf("usb: invalid EP0 max packet=%d\n", (int)packet);
    return -1;
  }

  input_size = 33U * xhci.context_size;
  output_size = 32U * xhci.context_size;

  usb_dma_sync_for_cpu(device->output_context, output_size);
  memset(device->input_context, 0, input_size);

  control = xhci_input_control(&xhci, device);
  slot_in = xhci_input_slot(&xhci, device);
  ep0_in = xhci_input_endpoint(&xhci, device, 1);
  slot_out = xhci_output_slot(&xhci, device);
  ep0_out = xhci_output_endpoint(&xhci, device, 1);

  //Solo se actualiza el Endpoint Context cero
  control[1] = USB_BIT(1);
  memmove(slot_in, slot_out, xhci.context_size);
  memmove(ep0_in, ep0_out, xhci.context_size);

  ep0_in[1] &= 0x0000ffffU;
  ep0_in[1] |= packet << XHCI_EP_MAX_PACKET_SHIFT;

  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_command_evaluate_context(slot_id) < 0)
    return -1;

  device->ep0_max_packet = packet;
  printf("usb: slot %d EP0 max packet updated to %d\n",
         (int)slot_id,
         (int)packet);
  return 0;
}

static int
usb_find_boot_keyboard(uchar *buffer,
                       uint32 length,
                       uchar *configuration,
                       uchar *interface_number,
                       uchar *endpoint_address,
                       uint16 *max_packet,
                       uchar *interval)
{
  uint32 offset;
  uchar descriptor_length;
  uchar descriptor_type;
  int matching_interface;
  struct usb_config_descriptor *config;
  struct usb_interface_descriptor *interface;
  struct usb_endpoint_descriptor *endpoint;

  if(length < sizeof(struct usb_config_descriptor))
    return -1;

  config = (struct usb_config_descriptor *)buffer;
  *configuration = config->bConfigurationValue;
  matching_interface = 0;
  offset = 0;

  while(offset + 2U <= length){
    descriptor_length = buffer[offset];
    descriptor_type = buffer[offset + 1U];

    if(descriptor_length < 2U || offset + descriptor_length > length)
      return -1;

    if(descriptor_type == USB_DT_INTERFACE &&
       descriptor_length >= sizeof(struct usb_interface_descriptor)){
      interface = (struct usb_interface_descriptor *)&buffer[offset];

      matching_interface =
        interface->bInterfaceClass == USB_CLASS_HID &&
        interface->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
        interface->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD;

      if(matching_interface){
        *interface_number = interface->bInterfaceNumber;
        printf("usb: HID Boot interface=%d endpoints=%d\n",
               (int)interface->bInterfaceNumber,
               (int)interface->bNumEndpoints);
      }
    } else if(descriptor_type == USB_DT_ENDPOINT &&
              matching_interface &&
              descriptor_length >= sizeof(struct usb_endpoint_descriptor)){
      endpoint = (struct usb_endpoint_descriptor *)&buffer[offset];

      if((endpoint->bEndpointAddress & USB_ENDPOINT_IN) != 0 &&
         (endpoint->bmAttributes & USB_ENDPOINT_TYPE_MASK) ==
           USB_ENDPOINT_INTERRUPT){
        *endpoint_address = endpoint->bEndpointAddress;
        *max_packet = usb_get_le16(&endpoint->wMaxPacketSize) & 0x7ffU;
        *interval = endpoint->bInterval;

        printf("usb: Interrupt IN endpoint=0x%x packet=%d interval=%d\n",
               endpoint->bEndpointAddress,
               (int)*max_packet,
               (int)*interval);
        return 0;
      }
    }

    offset += descriptor_length;
  }

  return -1;
}

static int
usb_find_hub_interface(struct usb_open_device *opened,
                       uchar *hub_protocol)
{
  uint32 offset;
  uchar descriptor_length;
  uchar descriptor_type;
  struct usb_interface_descriptor *interface;

  if(opened->descriptor.bDeviceClass == USB_CLASS_HUB){
    *hub_protocol = opened->descriptor.bDeviceProtocol;
    return 0;
  }

  offset = 0;
  while(offset + 2U <= opened->config_length){
    descriptor_length = opened->config_buffer[offset];
    descriptor_type = opened->config_buffer[offset + 1U];

    if(descriptor_length < 2U ||
       offset + descriptor_length > opened->config_length)
      return -1;

    if(descriptor_type == USB_DT_INTERFACE &&
       descriptor_length >= sizeof(struct usb_interface_descriptor)){
      interface = (struct usb_interface_descriptor *)
        &opened->config_buffer[offset];

      if(interface->bInterfaceClass == USB_CLASS_HUB){
        *hub_protocol = interface->bInterfaceProtocol;
        return 0;
      }
    }

    offset += descriptor_length;
  }

  return -1;
}

static uint32
usb_xhci_interval(uint32 speed, uchar interval)
{
  uint32 value;
  uint32 microframes;

  if(interval == 0)
    interval = 1;

  if(speed == XHCI_SPEED_HIGH || speed == XHCI_SPEED_SUPER){
    value = interval - 1U;
    if(value > 15U)
      value = 15U;
    return value;
  }

  //En Full/Low Speed se convierte el periodo en milisegundos a microframes
  microframes = (uint32)interval * 8U;
  value = 0;

  while(value < 15U && (1U << (value + 1U)) <= microframes)
    value++;

  return value;
}

static int
usb_configure_interrupt_endpoint(uint32 slot_id,
                                 uint32 speed,
                                 uchar endpoint_address,
                                 uint16 max_packet,
                                 uchar interval,
                                 uint32 *endpoint_id)
{
  struct xhci_device *device;
  uint32 ep_number;
  uint32 dci;
  uint32 *control;
  uint32 *slot_in;
  uint32 *endpoint_in;
  uint32 *slot_out;
  uint32 input_size;
  uint32 output_size;
  uint32 interval_value;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  ep_number = endpoint_address & USB_ENDPOINT_NUMBER_MASK;
  dci = (ep_number * 2U) +
        ((endpoint_address & USB_ENDPOINT_IN) ? 1U : 0U);

  if(dci < 2U || dci > 31U)
    return -1;

  input_size = 33U * xhci.context_size;
  output_size = 32U * xhci.context_size;

  usb_dma_sync_for_cpu(device->output_context, output_size);
  memset(device->input_context, 0, input_size);

  control = xhci_input_control(&xhci, device);
  slot_in = xhci_input_slot(&xhci, device);
  endpoint_in = xhci_input_endpoint(&xhci, device, dci);
  slot_out = xhci_output_slot(&xhci, device);

  control[1] = USB_BIT(0) | USB_BIT(dci);
  memmove(slot_in, slot_out, xhci.context_size);

  slot_in[0] &= ~(0x1fU << XHCI_SLOT_ENTRIES_SHIFT);
  slot_in[0] |= dci << XHCI_SLOT_ENTRIES_SHIFT;

  interval_value = usb_xhci_interval(speed, interval);

  endpoint_in[0] = interval_value << XHCI_EP_INTERVAL_SHIFT;
  endpoint_in[1] = (3U << XHCI_EP_CERR_SHIFT) |
                   (XHCI_EP_TYPE_INTERRUPT_IN << XHCI_EP_TYPE_SHIFT) |
                   ((uint32)max_packet << XHCI_EP_MAX_PACKET_SHIFT);
  endpoint_in[2] =
    (uint32)(usb_dma_address(device->interrupt_ring.trbs) | 1U);
  endpoint_in[3] =
    (uint32)(usb_dma_address(device->interrupt_ring.trbs) >> 32);
  endpoint_in[4] = (uint32)max_packet | ((uint32)max_packet << 16);

  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_command_configure_endpoint(slot_id) < 0)
    return -1;

  *endpoint_id = dci;
  printf("usb: slot %d xHCI endpoint DCI=%d interval-field=%d\n",
         (int)slot_id,
         (int)dci,
         (int)interval_value);
  return 0;
}

static int
usb_open_xhci_device(uint32 root_port_id,
                     uint32 speed,
                     uint32 route_string,
                     uint32 tt_hub_slot_id,
                     uint32 tt_port_id,
                     struct usb_open_device *opened)
{
  uint32 slot_id;
  uint32 packet;
  uint16 total_length;
  uchar *device_buffer;
  uchar *config_buffer;
  struct usb_config_descriptor *config;

  memset(opened, 0, sizeof(*opened));

  if(xhci_command_enable_slot(&slot_id) < 0)
    return -1;

  if(xhci_alloc_device(&xhci,
                       slot_id,
                       root_port_id,
                       speed,
                       route_string,
                       tt_hub_slot_id,
                       tt_port_id) < 0)
    return -1;

  if(xhci_command_address_device(slot_id) < 0)
    return -1;

  xhci_delay_us(2000U);

  device_buffer = usb_dma_alloc(64U, 64U);
  config_buffer = usb_dma_alloc(USB_XHCI_MAX_CONFIG, 64U);

  if(device_buffer == 0 || config_buffer == 0)
    return -1;

  if(usb_get_descriptor(slot_id,
                        USB_DT_DEVICE,
                        0,
                        device_buffer,
                        8) < 0){
    printf("usb: slot %d first Device Descriptor read failed\n",
           (int)slot_id);
    return -1;
  }

  packet = usb_ep0_packet_size(speed, device_buffer[7]);
  if(packet == 0 || usb_update_ep0_packet(slot_id, packet) < 0)
    return -1;

  memset(device_buffer, 0, 64U);
  if(usb_get_descriptor(slot_id,
                        USB_DT_DEVICE,
                        0,
                        device_buffer,
                        sizeof(struct usb_device_descriptor)) < 0)
    return -1;

  memmove(&opened->descriptor,
          device_buffer,
          sizeof(opened->descriptor));

  memset(config_buffer, 0, USB_XHCI_MAX_CONFIG);
  if(usb_get_descriptor(slot_id,
                        USB_DT_CONFIG,
                        0,
                        config_buffer,
                        sizeof(struct usb_config_descriptor)) < 0)
    return -1;

  config = (struct usb_config_descriptor *)config_buffer;
  total_length = usb_get_le16(&config->wTotalLength);

  if(total_length < sizeof(struct usb_config_descriptor) ||
     total_length > USB_XHCI_MAX_CONFIG){
    printf("usb: slot %d invalid configuration length=%d\n",
           (int)slot_id,
           (int)total_length);
    return -1;
  }

  memset(config_buffer, 0, USB_XHCI_MAX_CONFIG);
  if(usb_get_descriptor(slot_id,
                        USB_DT_CONFIG,
                        0,
                        config_buffer,
                        total_length) < 0)
    return -1;

  config = (struct usb_config_descriptor *)config_buffer;

  opened->slot_id = slot_id;
  opened->speed = speed;
  opened->configuration = config->bConfigurationValue;
  opened->config_length = total_length;
  opened->config_buffer = config_buffer;

  printf("usb: slot %d device %x:%x class=0x%x protocol=0x%x usb=0x%x configs=%d\n",
         (int)slot_id,
         (uint32)usb_get_le16(&opened->descriptor.idVendor),
         (uint32)usb_get_le16(&opened->descriptor.idProduct),
         (uint32)opened->descriptor.bDeviceClass,
         (uint32)opened->descriptor.bDeviceProtocol,
         (uint32)usb_get_le16(&opened->descriptor.bcdUSB),
         (int)opened->descriptor.bNumConfigurations);

  return 0;
}

static int
usb_finish_keyboard(struct usb_open_device *opened)
{
  uint32 endpoint_id = 0;
  uint16 max_packet = 0;
  uchar configuration = 0;
  uchar interface_number = 0;
  uchar endpoint_address = 0;
  uchar interval = 0;
  uchar *report_buffer;

  if(usb_find_boot_keyboard(opened->config_buffer,
                            opened->config_length,
                            &configuration,
                            &interface_number,
                            &endpoint_address,
                            &max_packet,
                            &interval) < 0)
    return -1;

  if(max_packet < 8U){
    printf("usb: keyboard endpoint packet is too small\n");
    return -1;
  }

  if(usb_set_configuration(opened->slot_id, configuration) < 0){
    printf("usb: Set Configuration failed for keyboard\n");
    return -1;
  }

  if(usb_hid_set_protocol(opened->slot_id, interface_number) < 0){
    printf("usb: Set Protocol Boot failed\n");
    return -1;
  }

  if(usb_hid_set_idle(opened->slot_id, interface_number) < 0){
    printf("usb: Set Idle failed\n");
    return -1;
  }

  if(usb_configure_interrupt_endpoint(opened->slot_id,
                                      opened->speed,
                                      endpoint_address,
                                      max_packet,
                                      interval,
                                      &endpoint_id) < 0)
    return -1;

  report_buffer = usb_dma_alloc(max_packet, 64U);
  if(report_buffer == 0)
    return -1;

  usb_kbd_attach(opened->slot_id,
                 endpoint_id,
                 interface_number,
                 interval,
                 max_packet,
                 report_buffer);

  if(xhci_queue_interrupt_in(opened->slot_id,
                             endpoint_id,
                             report_buffer,
                             max_packet) < 0)
    return -1;

  usb_keyboard.transfer_queued = 1;
  return 0;
}

static int
usb_hub_get_descriptor(uint32 slot_id, void *buffer, uint16 length)
{
  return xhci_control_transfer(slot_id,
                               USB_DIR_IN |
                               USB_TYPE_CLASS |
                               USB_RECIP_DEVICE,
                               USB_REQ_GET_DESCRIPTOR,
                               (uint16)USB_DT_HUB << 8,
                               0,
                               buffer,
                               length);
}

static int
usb_hub_port_feature(uint32 slot_id,
                     uint32 port_id,
                     uint16 feature,
                     int set)
{
  return xhci_control_transfer(slot_id,
                               USB_TYPE_CLASS |
                               USB_RECIP_OTHER,
                               set ? USB_REQ_SET_FEATURE :
                                     USB_REQ_CLEAR_FEATURE,
                               feature,
                               (uint16)port_id,
                               0,
                               0);
}

static int
usb_hub_get_port_status(uint32 slot_id,
                        uint32 port_id,
                        struct usb_port_status *status)
{
  memset(status, 0, sizeof(*status));

  return xhci_control_transfer(slot_id,
                               USB_DIR_IN |
                               USB_TYPE_CLASS |
                               USB_RECIP_OTHER,
                               USB_REQ_GET_STATUS,
                               0,
                               (uint16)port_id,
                               status,
                               sizeof(*status));
}

static void
usb_hub_clear_port_changes(uint32 slot_id,
                           uint32 port_id,
                           uint16 change)
{
  if(change & USB_PORT_STAT_C_CONNECTION)
    usb_hub_port_feature(slot_id,
                         port_id,
                         USB_PORT_FEAT_C_CONNECTION,
                         0);

  if(change & USB_PORT_STAT_C_ENABLE)
    usb_hub_port_feature(slot_id,
                         port_id,
                         USB_PORT_FEAT_C_ENABLE,
                         0);

  if(change & USB_PORT_STAT_C_SUSPEND)
    usb_hub_port_feature(slot_id,
                         port_id,
                         USB_PORT_FEAT_C_SUSPEND,
                         0);

  if(change & USB_PORT_STAT_C_OVERCURRENT)
    usb_hub_port_feature(slot_id,
                         port_id,
                         USB_PORT_FEAT_C_OVER_CURRENT,
                         0);

  if(change & USB_PORT_STAT_C_RESET)
    usb_hub_port_feature(slot_id,
                         port_id,
                         USB_PORT_FEAT_C_RESET,
                         0);
}

static int
usb_mark_xhci_hub(uint32 slot_id,
                  uint32 ports,
                  uint32 tt_think_time,
                  int multi_tt)
{
  struct xhci_device *device;
  uint32 *control;
  uint32 *slot_in;
  uint32 *slot_out;
  uint32 input_size;
  uint32 output_size;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  input_size = 33U * xhci.context_size;
  output_size = 32U * xhci.context_size;

  usb_dma_sync_for_cpu(device->output_context, output_size);
  memset(device->input_context, 0, input_size);

  control = xhci_input_control(&xhci, device);
  slot_in = xhci_input_slot(&xhci, device);
  slot_out = xhci_output_slot(&xhci, device);

  //Evaluate Context modifica únicamente el Slot Context del hub
  control[1] = USB_BIT(0);
  memmove(slot_in, slot_out, xhci.context_size);

  slot_in[0] |= XHCI_SLOT_HUB;
  if(multi_tt)
    slot_in[0] |= XHCI_SLOT_MTT;
  else
    slot_in[0] &= ~XHCI_SLOT_MTT;

  slot_in[1] &= ~(0xffU << XHCI_SLOT_MAX_PORTS_SHIFT);
  slot_in[1] |= (ports & 0xffU) << XHCI_SLOT_MAX_PORTS_SHIFT;

  slot_in[2] &= ~(0x3U << XHCI_SLOT_TT_THINK_SHIFT);
  slot_in[2] |= (tt_think_time & 0x3U) << XHCI_SLOT_TT_THINK_SHIFT;

  //El Device State del Slot Context de entrada debe permanecer a cero
  slot_in[3] = 0;

  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_command_configure_endpoint(slot_id) < 0)
    return -1;

  device->is_hub = 1;
  device->hub_ports = ports;
  device->hub_tt_think_time = tt_think_time;
  device->hub_multi_tt = multi_tt;

  printf("usb-hub: xHCI slot=%d ports=%d tt-think=%d multi-tt=%d\n",
         (int)slot_id,
         (int)ports,
         (int)tt_think_time,
         multi_tt);
  return 0;
}

static uint32
usb_hub_port_speed(uint16 status)
{
  if(status & USB_PORT_STAT_HIGH_SPEED)
    return XHCI_SPEED_HIGH;

  if(status & USB_PORT_STAT_LOW_SPEED)
    return XHCI_SPEED_LOW;

  return XHCI_SPEED_FULL;
}

static int
usb_hub_wait_for_connection(uint32 hub_slot_id,
                            uint32 port_id,
                            struct usb_port_status *status_buffer,
                            uint16 *status,
                            uint16 *change)
{
  uint32 attempt;

  for(attempt = 0; attempt < 20U; attempt++){
    if(usb_hub_get_port_status(hub_slot_id,
                               port_id,
                               status_buffer) < 0)
      return -1;

    *status = usb_get_le16(&status_buffer->wPortStatus);
    *change = usb_get_le16(&status_buffer->wPortChange);

    if(*status & USB_PORT_STAT_CONNECTION)
      return 1;

    if(attempt + 1U < 20U)
      xhci_delay_us(25000U);
  }

  return 0;
}

static int
usb_hub_reset_port(uint32 hub_slot_id,
                   uint32 port_id,
                   struct usb_port_status *status_buffer,
                   uint32 *speed)
{
  uint32 attempt;
  uint32 delay_us;
  uint16 status;
  uint16 change;

  if(usb_hub_get_port_status(hub_slot_id,
                             port_id,
                             status_buffer) < 0)
    return -1;

  status = usb_get_le16(&status_buffer->wPortStatus);
  change = usb_get_le16(&status_buffer->wPortChange);
  usb_hub_clear_port_changes(hub_slot_id, port_id, change);

  if((status & USB_PORT_STAT_CONNECTION) == 0)
    return -1;

  for(attempt = 0; attempt < 5U; attempt++){
    if(usb_hub_port_feature(hub_slot_id,
                            port_id,
                            USB_PORT_FEAT_RESET,
                            1) < 0){
      printf("usb-hub: port %d reset request failed\n", (int)port_id);
      return -1;
    }

    delay_us = attempt == 0 ? 20000U : 200000U;
    xhci_delay_us(delay_us);

    if(usb_hub_get_port_status(hub_slot_id,
                               port_id,
                               status_buffer) < 0)
      return -1;

    status = usb_get_le16(&status_buffer->wPortStatus);
    change = usb_get_le16(&status_buffer->wPortChange);

    if((status & USB_PORT_STAT_CONNECTION) == 0){
      printf("usb-hub: port %d disconnected during reset\n",
             (int)port_id);
      return -1;
    }

    if(status & USB_PORT_STAT_ENABLE)
      break;
  }

  if((status & USB_PORT_STAT_ENABLE) == 0){
    printf("usb-hub: port %d reset timeout status=0x%x change=0x%x\n",
           (int)port_id,
           (uint32)status,
           (uint32)change);
    return -1;
  }

  usb_hub_clear_port_changes(hub_slot_id, port_id, change);
  *speed = usb_hub_port_speed(status);

  //USB 2.0 exige 10 ms de recuperación antes de Address Device
  xhci_delay_us(10000U);

  printf("usb-hub: port %d reset complete status=0x%x change=0x%x speed=%d\n",
         (int)port_id,
         (uint32)status,
         (uint32)change,
         (int)*speed);
  return 0;
}

static int
usb_enumerate_keyboard_behind_hub(struct usb_open_device *hub,
                                  uchar hub_protocol)
{
  uint32 power_mode;
  int power_result;
  struct xhci_device *hub_device;
  struct usb_hub_descriptor *hub_descriptor;
  struct usb_port_status *port_status;
  struct usb_open_device child;
  uint32 port_id;
  uint32 ports;
  uint32 delay_ms;
  uint32 child_speed;
  uint32 route_string;
  uint32 tt_slot;
  uint32 tt_port;
  uint16 characteristics;
  uint16 status;
  uint16 change;
  int multi_tt;

  child_speed = 0;
  route_string = 0;
  tt_slot = 0;
  tt_port = 0;
  characteristics = 0;
  status = 0;
  change = 0;

  /*
  El hub interno 2109:3431 utiliza Single-TT.
  Se conserva también la comprobación por si el protocolo indicase Multi-TT.
  */
  multi_tt = hub_protocol == 2U;

  hub_device = xhci_get_device(&xhci, hub->slot_id);
  if(hub_device == 0){
    printf("usb-hub: xHCI device for hub slot %d not found\n",
           (int)hub->slot_id);
    return -1;
  }

  if(hub->speed != XHCI_SPEED_HIGH){
    printf("usb-hub: only the internal High-Speed USB 2.0 hub is supported\n");
    return -1;
  }

  /*
  Activa la configuración que contiene la interfaz de hub.
  */
  if(usb_set_configuration(hub->slot_id,
                           hub->configuration) < 0){
    printf("usb-hub: Set Configuration failed\n");
    return -1;
  }

  /*
  Reserva buffers DMA para el descriptor del hub y para consultar
  el estado de sus puertos descendentes.
  */
  hub_descriptor = usb_dma_alloc(64U, 64U);
  port_status = usb_dma_alloc(sizeof(*port_status), 64U);

  if(hub_descriptor == 0 || port_status == 0){
    printf("usb-hub: DMA allocation failed\n");
    return -1;
  }

  memset(hub_descriptor, 0, 64U);
  memset(port_status, 0, sizeof(*port_status));

  /*
  Lee el descriptor específico del hub USB 2.0.
  */
  if(usb_hub_get_descriptor(hub->slot_id,
                            hub_descriptor,
                            64U) < 0){
    printf("usb-hub: GET_HUB_DESCRIPTOR failed\n");
    return -1;
  }

  if(hub_descriptor->bLength < 7U ||
     hub_descriptor->bDescriptorType != USB_DT_HUB){
    printf("usb-hub: invalid descriptor length=%d type=0x%x\n",
           (int)hub_descriptor->bLength,
           (uint32)hub_descriptor->bDescriptorType);
    return -1;
  }

  ports = hub_descriptor->bNbrPorts;

  if(ports == 0U || ports > USB_HUB_MAX_PORTS){
    printf("usb-hub: unsupported downstream port count=%d\n",
           (int)ports);
    return -1;
  }

  characteristics =
    usb_get_le16(&hub_descriptor->wHubCharacteristics);

  printf("usb-hub: VIA internal hub slot=%d ports=%d protocol=%d characteristics=0x%x pgood=%d\n",
         (int)hub->slot_id,
         (int)ports,
         (int)hub_protocol,
         (uint32)characteristics,
         (int)hub_descriptor->bPwrOn2PwrGood);

  /*
  Actualiza el Slot Context del hub para indicar al xHCI que el
  dispositivo es un hub High-Speed y que puede actuar como TT.
  */
  if(usb_mark_xhci_hub(hub->slot_id,
                       ports,
                       (characteristics >> 5) & 0x3U,
                       multi_tt) < 0){
    printf("usb-hub: could not update xHCI hub context\n");
    return -1;
  }

  power_mode = characteristics & 0x3U;

  /*
  Primero se consulta el estado del puerto uno sin modificar
  la alimentación del hub.
  */
  memset(port_status, 0, sizeof(*port_status));

  if(usb_hub_get_port_status(hub->slot_id,
                             1,
                             port_status) < 0){
    printf("usb-hub: pre-power GET_STATUS failed\n");
    return -1;
  }

  status = usb_get_le16(&port_status->wPortStatus);
  change = usb_get_le16(&port_status->wPortChange);

  printf("usb-hub: pre-power port 1 status=0x%x change=0x%x powered=%d overcurrent=%d\n",
         (uint32)status,
         (uint32)change,
         (status & USB_PORT_STAT_POWER) != 0,
         (status & USB_PORT_STAT_OVERCURRENT) != 0);

  printf("usb-hub: port power mode=%d\n",
         (int)power_mode);

    /*
  U-Boot solicita PORT_POWER para todos los puertos del hub,
  incluso cuando el descriptor indica alimentación agrupada.
  Antes se limpian los cambios de sobrecorriente pendientes.
  */

  printf("usb-hub: clearing previous overcurrent changes\n");

  for(port_id = 1; port_id <= ports; port_id++){
    memset(port_status, 0, sizeof(*port_status));

    if(usb_hub_get_port_status(hub->slot_id,
                               port_id,
                               port_status) < 0){
      printf("usb-hub: pre-power status failed port=%d\n",
             (int)port_id);
      continue;
    }

    status = usb_get_le16(&port_status->wPortStatus);
    change = usb_get_le16(&port_status->wPortChange);

    printf("usb-hub: pre-power port=%d status=0x%x change=0x%x\n",
           (int)port_id,
           (uint32)status,
           (uint32)change);

    if(change & USB_PORT_STAT_C_OVERCURRENT){
      printf("usb-hub: clearing C_PORT_OVER_CURRENT port=%d\n",
             (int)port_id);

      if(usb_hub_port_feature(hub->slot_id,
                              port_id,
                              USB_PORT_FEAT_C_OVER_CURRENT,
                              0) < 0){
        printf("usb-hub: failed to clear overcurrent change port=%d\n",
               (int)port_id);
      }
    }
  }

  /*
  Deja un pequeño tiempo para que el hub actualice los bits
  de cambio antes de intentar alimentar los puertos.
  */
  xhci_delay_us(20000U);

  printf("usb-hub: enabling power on all downstream ports\n");

  for(port_id = 1; port_id <= ports; port_id++){
    printf("usb-hub: before PORT_POWER port=%d\n",
           (int)port_id);

    power_result = usb_hub_port_feature(hub->slot_id,
                                        port_id,
                                        USB_PORT_FEAT_POWER,
                                        1);

    printf("usb-hub: after PORT_POWER port=%d result=%d\n",
           (int)port_id,
           power_result);

    if(power_result < 0){
      printf("usb-hub: PORT_POWER failed port=%d\n",
             (int)port_id);
      return -1;
    }
  }

  if(delay_ms < 100U)
    delay_ms = 100U;

  printf("usb-hub: waiting %d ms before scanning ports\n",
         (int)delay_ms);

  xhci_delay_us(delay_ms * 1000U);

  printf("usb-hub: port power delay completed\n");

  /*
  Recorre los puertos descendentes buscando el primer dispositivo
  conectado que pueda enumerarse como teclado HID Boot.
  */
  for(port_id = 1; port_id <= ports; port_id++){
    memset(port_status, 0, sizeof(*port_status));

    if(usb_hub_wait_for_connection(hub->slot_id,
                                   port_id,
                                   port_status,
                                   &status,
                                   &change) < 0){
      printf("usb-hub: could not read port %d status\n",
             (int)port_id);
      continue;
    }

    printf("usb-hub: port %d status=0x%x change=0x%x connected=%d powered=%d overcurrent=%d\n",
           (int)port_id,
           (uint32)status,
           (uint32)change,
           (status & USB_PORT_STAT_CONNECTION) != 0,
           (status & USB_PORT_STAT_POWER) != 0,
           (status & USB_PORT_STAT_OVERCURRENT) != 0);

    /*
    Limpia los indicadores de cambio comunicados por el hub.
    */
    usb_hub_clear_port_changes(hub->slot_id,
                               port_id,
                               change);

    if((status & USB_PORT_STAT_CONNECTION) == 0)
      continue;

    /*
    Reinicia el puerto descendente y obtiene la velocidad
    del dispositivo conectado.
    */
    if(usb_hub_reset_port(hub->slot_id,
                          port_id,
                          port_status,
                          &child_speed) < 0){
      printf("usb-hub: reset failed on downstream port %d\n",
             (int)port_id);
      continue;
    }

    /*
    Solo se soporta un nivel de hub. El primer nibble de la Route
    String contiene el número del puerto descendente.
    */
    route_string = port_id & 0xfU;
    tt_slot = 0;
    tt_port = 0;

    /*
    Los dispositivos Full-Speed y Low-Speed necesitan utilizar
    el Transaction Translator del hub High-Speed.
    */
    if(child_speed == XHCI_SPEED_FULL ||
       child_speed == XHCI_SPEED_LOW){
      tt_slot = hub->slot_id;
      tt_port = port_id;
    }

    printf("usb-hub: enumerating child port=%d route=0x%x speed=%d tt-slot=%d tt-port=%d\n",
           (int)port_id,
           (int)route_string,
           (int)child_speed,
           (int)tt_slot,
           (int)tt_port);

    /*
    Crea un segundo slot xHCI para el dispositivo situado
    detrás del hub interno.
    */
    if(usb_open_xhci_device(hub_device->root_port_id,
                            child_speed,
                            route_string,
                            tt_slot,
                            tt_port,
                            &child) < 0){
      printf("usb-hub: child enumeration failed on port %d\n",
             (int)port_id);
      continue;
    }

    /*
    Comprueba si el dispositivo contiene una interfaz HID Boot
    Keyboard y configura su endpoint Interrupt IN.
    */
    if(usb_finish_keyboard(&child) == 0){
      printf("usb-hub: HID Boot keyboard ready on downstream port %d\n",
             (int)port_id);
      return 0;
    }

    printf("usb-hub: device on downstream port %d is not a HID Boot keyboard\n",
           (int)port_id);
  }

  printf("usb-hub: no HID Boot keyboard found behind internal hub\n");

  return -1;
}

int
usb_enumerate_keyboard(uint32 port_id, uint32 speed)
{
  struct usb_open_device root_device;
  uchar hub_protocol = 0;

  printf("\nusb: enumerating root port=%d speed=%d\n",
         (int)port_id,
         (int)speed);

  if(usb_open_xhci_device(port_id,
                          speed,
                          0,
                          0,
                          0,
                          &root_device) < 0)
    return -1;

  //Se conserva la posibilidad de un teclado conectado directamente
  if(usb_finish_keyboard(&root_device) == 0)
    return 0;

  if(usb_find_hub_interface(&root_device, &hub_protocol) < 0){
    printf("usb: device is neither a HID Boot keyboard nor a USB hub\n");
    return -1;
  }

  if(hub_protocol != USB_HUB_PROTOCOL_SINGLE_TT){
    printf("usb-hub: only the internal Single-TT USB 2.0 hub is supported, protocol=%d\n",
           (int)hub_protocol);
    return -1;
  }

  if(usb_enumerate_keyboard_behind_hub(&root_device,
                                        hub_protocol) < 0){
    printf("usb-hub: no HID Boot keyboard found behind internal hub\n");
    return -1;
  }

  return 0;
}
