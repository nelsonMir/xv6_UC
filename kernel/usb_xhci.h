
/*
Basado en el controlador xHCI de Linux y U-Boot.

*/

/*
usb_xhci.h

Ruta USB mínima para xv6 en la VisionFive 2.

El código está inspirado en la implementación xHCI de U-Boot fijada por
StarFive para la VisionFive 2, pero se ha reducido para soportar únicamente:

- VIA VL805 conectado a PCIe0.
- El hub USB 2.0 interno VIA 2109:3431.
- Un teclado USB HID Boot conectado a uno de sus cuatro puertos.
- Transferencias de control e Interrupt IN.
- Polling sin interrupciones del xHCI.
*/

#ifndef XV6_USB_XHCI_H
#define XV6_USB_XHCI_H

#include "types.h"

#define USB_BIT(n)                 (1U << (n))
#define USB_DMA_AREA_SIZE          (512U * 1024U)
#define USB_DMA_CACHE_LINE         64U
#define USB_XHCI_PAGE_SIZE         4096U
#define USB_XHCI_RING_TRBS         256U
#define USB_XHCI_EVENT_TRBS        256U
#define USB_XHCI_MAX_CONFIG        512U
#define USB_XHCI_MAX_SLOTS_USED    8U

//Registros de capacidades
#define XHCI_CAPLENGTH             0x00U
#define XHCI_HCSPARAMS1            0x04U
#define XHCI_HCSPARAMS2            0x08U
#define XHCI_HCSPARAMS3            0x0cU
#define XHCI_HCCPARAMS1            0x10U
#define XHCI_DBOFF                 0x14U
#define XHCI_RTSOFF                0x18U

//Registros operacionales
#define XHCI_USBCMD                0x00U
#define XHCI_USBSTS                0x04U
#define XHCI_PAGESIZE              0x08U
#define XHCI_DNCTRL                0x14U
#define XHCI_CRCR                  0x18U
#define XHCI_DCBAAP                0x30U
#define XHCI_CONFIG                0x38U
#define XHCI_PORT_BASE             0x400U
#define XHCI_PORT_STRIDE           0x10U

//Bits de USBCMD
#define XHCI_CMD_RUN               USB_BIT(0)
#define XHCI_CMD_HCRST             USB_BIT(1)

//Bits de USBSTS
#define XHCI_STS_HCH               USB_BIT(0)
#define XHCI_STS_HSE               USB_BIT(2)
#define XHCI_STS_EINT              USB_BIT(3)
#define XHCI_STS_PCD               USB_BIT(4)
#define XHCI_STS_CNR               USB_BIT(11)
#define XHCI_STS_HCE               USB_BIT(12)

//Registros del interrupter cero
#define XHCI_INTR_BASE             0x20U
#define XHCI_IMAN                  0x00U
#define XHCI_IMOD                  0x04U
#define XHCI_ERSTSZ                0x08U
#define XHCI_ERSTBA                0x10U
#define XHCI_ERDP                  0x18U
#define XHCI_ERDP_EHB              USB_BIT(3)

//Bits de PORTSC
#define XHCI_PORT_CCS              USB_BIT(0)
#define XHCI_PORT_PED              USB_BIT(1)
#define XHCI_PORT_PR               USB_BIT(4)
#define XHCI_PORT_PLS_SHIFT        5U
#define XHCI_PORT_PLS_MASK         (0xfU << XHCI_PORT_PLS_SHIFT)
#define XHCI_PORT_PP               USB_BIT(9)
#define XHCI_PORT_SPEED_SHIFT      10U
#define XHCI_PORT_SPEED_MASK       (0xfU << XHCI_PORT_SPEED_SHIFT)
#define XHCI_PORT_CSC              USB_BIT(17)
#define XHCI_PORT_PEC              USB_BIT(18)
#define XHCI_PORT_WRC              USB_BIT(19)
#define XHCI_PORT_OCC              USB_BIT(20)
#define XHCI_PORT_PRC              USB_BIT(21)
#define XHCI_PORT_PLC              USB_BIT(22)
#define XHCI_PORT_CEC              USB_BIT(23)
#define XHCI_PORT_WPR              USB_BIT(31)
#define XHCI_PORT_CHANGE_BITS      (XHCI_PORT_CSC | XHCI_PORT_PEC | \
                                    XHCI_PORT_WRC | XHCI_PORT_OCC | \
                                    XHCI_PORT_PRC | XHCI_PORT_PLC | \
                                    XHCI_PORT_CEC)

//Bits de solo lectura y de lectura/escritura que deben conservarse al escribir PORTSC
#define XHCI_PORT_RO               (USB_BIT(0) | USB_BIT(3) | \
                                    (0xfU << 10) | USB_BIT(30))
#define XHCI_PORT_RWS              ((0xfU << 5) | USB_BIT(9) | \
                                    (3U << 14) | (7U << 25))
#define XHCI_PORT_RW1CS            (USB_BIT(1) | (0x7fU << 17))

//Velocidades indicadas por PORTSC
#define XHCI_SPEED_FULL            1U
#define XHCI_SPEED_LOW             2U
#define XHCI_SPEED_HIGH            3U
#define XHCI_SPEED_SUPER           4U

//Formato común de los TRB
#define XHCI_TRB_CYCLE             USB_BIT(0)
#define XHCI_TRB_ENT               USB_BIT(1)
#define XHCI_TRB_ISP               USB_BIT(2)
#define XHCI_TRB_CHAIN             USB_BIT(4)
#define XHCI_TRB_IOC               USB_BIT(5)
#define XHCI_TRB_IDT               USB_BIT(6)
#define XHCI_TRB_TC                USB_BIT(1)
#define XHCI_TRB_DIR_IN            USB_BIT(16)
#define XHCI_TRB_TYPE_SHIFT        10U
#define XHCI_TRB_TYPE_MASK         (0x3fU << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE(type)        ((uint32)(type) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_SLOT_SHIFT        24U
#define XHCI_TRB_EP_SHIFT          16U
#define XHCI_TRB_COMPLETION_SHIFT  24U
#define XHCI_TRB_LENGTH_MASK       0x00ffffffU

//Tipos de TRB utilizados por esta ruta
#define XHCI_TRB_NORMAL            1U
#define XHCI_TRB_SETUP             2U
#define XHCI_TRB_DATA              3U
#define XHCI_TRB_STATUS            4U
#define XHCI_TRB_LINK              6U
#define XHCI_TRB_ENABLE_SLOT       9U
#define XHCI_TRB_ADDRESS_DEVICE    11U
#define XHCI_TRB_CONFIG_EP         12U
#define XHCI_TRB_EVALUATE_CTX      13U
#define XHCI_TRB_TRANSFER_EVENT    32U
#define XHCI_TRB_COMMAND_EVENT     33U
#define XHCI_TRB_PORT_EVENT        34U

//Campo TRT del Setup Stage TRB
#define XHCI_SETUP_TRT_SHIFT       16U
#define XHCI_SETUP_TRT_NONE        (0U << XHCI_SETUP_TRT_SHIFT)
#define XHCI_SETUP_TRT_OUT         (2U << XHCI_SETUP_TRT_SHIFT)
#define XHCI_SETUP_TRT_IN          (3U << XHCI_SETUP_TRT_SHIFT)

//Códigos de finalización aceptados
#define XHCI_CC_SUCCESS            1U
#define XHCI_CC_SHORT_PACKET       13U

//Campos de los contextos xHCI
#define XHCI_SLOT_SPEED_SHIFT      20U
#define XHCI_SLOT_ENTRIES_SHIFT    27U
#define XHCI_SLOT_ROOT_PORT_SHIFT  16U
#define XHCI_SLOT_ROUTE_MASK       0x000fffffU
#define XHCI_SLOT_MTT              USB_BIT(25)
#define XHCI_SLOT_HUB              USB_BIT(26)
#define XHCI_SLOT_MAX_PORTS_SHIFT  24U
#define XHCI_SLOT_TT_SLOT_SHIFT    0U
#define XHCI_SLOT_TT_PORT_SHIFT    8U
#define XHCI_SLOT_TT_THINK_SHIFT   16U
#define XHCI_EP_CERR_SHIFT         1U
#define XHCI_EP_TYPE_SHIFT         3U
#define XHCI_EP_INTERVAL_SHIFT     16U
#define XHCI_EP_MAX_PACKET_SHIFT   16U
#define XHCI_EP_TYPE_CONTROL       4U
#define XHCI_EP_TYPE_INTERRUPT_IN  7U

//Peticiones USB estándar y HID
#define USB_DIR_IN                 0x80U
#define USB_TYPE_STANDARD          0x00U
#define USB_TYPE_CLASS             0x20U
#define USB_RECIP_DEVICE           0x00U
#define USB_RECIP_INTERFACE        0x01U
#define USB_RECIP_OTHER            0x03U
#define USB_REQ_GET_STATUS         0x00U
#define USB_REQ_CLEAR_FEATURE      0x01U
#define USB_REQ_SET_FEATURE        0x03U
#define USB_REQ_GET_DESCRIPTOR     0x06U
#define USB_REQ_SET_CONFIGURATION  0x09U
#define USB_REQ_SET_IDLE           0x0aU
#define USB_REQ_SET_PROTOCOL       0x0bU
#define USB_DT_DEVICE              0x01U
#define USB_DT_CONFIG              0x02U
#define USB_DT_INTERFACE           0x04U
#define USB_DT_ENDPOINT            0x05U
#define USB_DT_HUB                 0x29U
#define USB_CLASS_HID              0x03U
#define USB_CLASS_HUB              0x09U
#define USB_SUBCLASS_BOOT          0x01U
#define USB_PROTOCOL_KEYBOARD      0x01U
#define USB_ENDPOINT_IN            0x80U
#define USB_ENDPOINT_NUMBER_MASK   0x0fU
#define USB_ENDPOINT_TYPE_MASK     0x03U
#define USB_ENDPOINT_INTERRUPT     0x03U

//Características y estado del hub USB 2.0
#define USB_HUB_MAX_PORTS          4U
#define USB_HUB_PROTOCOL_SINGLE_TT 1U
#define USB_HUB_PROTOCOL_MULTI_TT  2U
#define USB_PORT_FEAT_ENABLE       1U
#define USB_PORT_FEAT_SUSPEND      2U
#define USB_PORT_FEAT_RESET        4U
#define USB_PORT_FEAT_POWER        8U
#define USB_PORT_FEAT_C_CONNECTION 16U
#define USB_PORT_FEAT_C_ENABLE     17U
#define USB_PORT_FEAT_C_SUSPEND    18U
#define USB_PORT_FEAT_C_OVER_CURRENT 19U
#define USB_PORT_FEAT_C_RESET      20U
#define USB_PORT_STAT_CONNECTION   USB_BIT(0)
#define USB_PORT_STAT_ENABLE       USB_BIT(1)
#define USB_PORT_STAT_SUSPEND      USB_BIT(2)
#define USB_PORT_STAT_OVERCURRENT  USB_BIT(3)
#define USB_PORT_STAT_RESET        USB_BIT(4)
#define USB_PORT_STAT_POWER        USB_BIT(8)
#define USB_PORT_STAT_LOW_SPEED    USB_BIT(9)
#define USB_PORT_STAT_HIGH_SPEED   USB_BIT(10)
#define USB_PORT_STAT_C_CONNECTION USB_BIT(0)
#define USB_PORT_STAT_C_ENABLE     USB_BIT(1)
#define USB_PORT_STAT_C_SUSPEND    USB_BIT(2)
#define USB_PORT_STAT_C_OVERCURRENT USB_BIT(3)
#define USB_PORT_STAT_C_RESET      USB_BIT(4)

struct xhci_trb {
  uint64 parameter;
  uint32 status;
  uint32 control;
} __attribute__((packed, aligned(16)));

struct xhci_erst_entry {
  uint64 address;
  uint32 size;
  uint32 reserved;
} __attribute__((packed, aligned(16)));

struct xhci_ring {
  struct xhci_trb *trbs;
  uint32 count;
  uint32 enqueue;
  uint32 dequeue;
  uint32 producer_cycle;
  uint32 consumer_cycle;
  int has_link;
};

struct xhci_device {
  int allocated;
  uint32 slot_id;
  uint32 root_port_id;
  uint32 speed;
  uint32 route_string;
  uint32 tt_hub_slot_id;
  uint32 tt_port_id;
  uint32 ep0_max_packet;
  uint32 context_size;
  int is_hub;
  uint32 hub_ports;
  uint32 hub_tt_think_time;
  int hub_multi_tt;
  uchar *input_context;
  uchar *output_context;
  struct xhci_ring ep0_ring;
  struct xhci_ring interrupt_ring;
};

struct xhci_controller {
  uint64 cap_base;
  uint64 op_base;
  uint64 db_base;
  uint64 rt_base;
  uint32 max_slots;
  uint32 max_ports;
  uint32 max_interrupters;
  uint32 context_size;
  uint32 page_size;
  int ready;

  uint64 *dcbaa;
  struct xhci_ring command_ring;
  struct xhci_ring event_ring;
  struct xhci_erst_entry *erst;
  uint64 *scratchpad_array;
  uint32 scratchpad_count;

  struct xhci_device devices[USB_XHCI_MAX_SLOTS_USED + 1U];
};

struct usb_device_descriptor {
  uchar bLength;
  uchar bDescriptorType;
  uint16 bcdUSB;
  uchar bDeviceClass;
  uchar bDeviceSubClass;
  uchar bDeviceProtocol;
  uchar bMaxPacketSize0;
  uint16 idVendor;
  uint16 idProduct;
  uint16 bcdDevice;
  uchar iManufacturer;
  uchar iProduct;
  uchar iSerialNumber;
  uchar bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor {
  uchar bLength;
  uchar bDescriptorType;
  uint16 wTotalLength;
  uchar bNumInterfaces;
  uchar bConfigurationValue;
  uchar iConfiguration;
  uchar bmAttributes;
  uchar bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
  uchar bLength;
  uchar bDescriptorType;
  uchar bInterfaceNumber;
  uchar bAlternateSetting;
  uchar bNumEndpoints;
  uchar bInterfaceClass;
  uchar bInterfaceSubClass;
  uchar bInterfaceProtocol;
  uchar iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
  uchar bLength;
  uchar bDescriptorType;
  uchar bEndpointAddress;
  uchar bmAttributes;
  uint16 wMaxPacketSize;
  uchar bInterval;
} __attribute__((packed));


struct usb_hub_descriptor {
  uchar bLength;
  uchar bDescriptorType;
  uchar bNbrPorts;
  uint16 wHubCharacteristics;
  uchar bPwrOn2PwrGood;
  uchar bHubContrCurrent;
  uchar variable[8];
} __attribute__((packed));

struct usb_port_status {
  uint16 wPortStatus;
  uint16 wPortChange;
} __attribute__((packed));

struct usb_keyboard {
  int present;
  uint32 slot_id;
  uint32 endpoint_id;
  uint32 interface_number;
  uint32 interval;
  uint32 max_packet;
  uchar *report_buffer;
  uchar previous[8];
  int transfer_queued;
};

extern struct xhci_controller xhci;
extern struct usb_keyboard usb_keyboard;

//Memoria DMA
void   usb_dma_reset(void);
void  *usb_dma_alloc(uint32 size, uint32 alignment);
uint64 usb_dma_address(const void *pointer);
void   usb_dma_sync_for_device(const void *pointer, uint32 size);
void   usb_dma_sync_for_cpu(const void *pointer, uint32 size);
int    xhci_mem_init(struct xhci_controller *controller);
struct xhci_device *xhci_get_device(struct xhci_controller *controller,
                                    uint32 slot_id);
int    xhci_alloc_device(struct xhci_controller *controller,
                         uint32 slot_id,
                         uint32 root_port_id,
                         uint32 speed,
                         uint32 route_string,
                         uint32 tt_hub_slot_id,
                         uint32 tt_port_id);
uint32 *xhci_input_control(struct xhci_controller *controller,
                           struct xhci_device *device);
uint32 *xhci_input_slot(struct xhci_controller *controller,
                        struct xhci_device *device);
uint32 *xhci_input_endpoint(struct xhci_controller *controller,
                            struct xhci_device *device,
                            uint32 endpoint_id);
uint32 *xhci_output_slot(struct xhci_controller *controller,
                         struct xhci_device *device);
uint32 *xhci_output_endpoint(struct xhci_controller *controller,
                             struct xhci_device *device,
                             uint32 endpoint_id);

//MMIO y tiempo
uint32 xhci_read32(uint64 address);
void   xhci_write32(uint64 address, uint32 value);
uint64 xhci_read64(uint64 address);
void   xhci_write64(uint64 address, uint64 value);
void   xhci_delay_us(uint32 microseconds);
int    xhci_wait32(uint64 address,
                   uint32 mask,
                   uint32 expected,
                   uint32 timeout_us);

//Command Ring, Event Ring y transferencias
int xhci_command_enable_slot(uint32 *slot_id);
int xhci_command_address_device(uint32 slot_id);
int xhci_command_evaluate_context(uint32 slot_id);
int xhci_command_configure_endpoint(uint32 slot_id);
int xhci_control_transfer(uint32 slot_id,
                          uchar request_type,
                          uchar request,
                          uint16 value,
                          uint16 index,
                          void *buffer,
                          uint16 length);
int xhci_queue_interrupt_in(uint32 slot_id,
                            uint32 endpoint_id,
                            void *buffer,
                            uint32 length);
int xhci_poll_keyboard_transfer(uint32 slot_id,
                                uint32 endpoint_id,
                                uint32 *actual_length);

//Inicialización y puertos
int    xhci_min_init(uint64 bar);
int    xhci_scan_root_ports(void);
uint32 xhci_port_speed(uint32 port_id);

//Enumeración y teclado
int  usb_enumerate_keyboard(uint32 port_id, uint32 speed);
void usb_kbd_attach(uint32 slot_id,
                    uint32 endpoint_id,
                    uint32 interface_number,
                    uint32 interval,
                    uint32 max_packet,
                    uchar *report_buffer);
void usb_kbd_poll(void);
int  usb_kbd_present(void);

#endif
