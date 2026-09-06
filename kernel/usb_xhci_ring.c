
/*
Basado en xhci-ring.c de U-Boot y en el controlador xHCI de Linux.

*/

/*
usb_xhci_ring.c

Command Ring, Event Ring y las dos clases de transferencia necesarias:
control para enumerar el dispositivo e Interrupt IN para el teclado.
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "usb_xhci.h"

#ifndef DBG_XHCI
#define DBG_XHCI 0
#endif

#define XHCI_DEBUG(...)            \
  do {                             \
    if(DBG_XHCI)                   \
      printf(__VA_ARGS__);         \
  } while(0)

#define XHCI_EVENT_TIMEOUT_US 2000000U

static uint32
xhci_trb_get_type(const struct xhci_trb *trb)
{
  return (trb->control & XHCI_TRB_TYPE_MASK) >>
         XHCI_TRB_TYPE_SHIFT;
}

static uint32
xhci_trb_get_completion(const struct xhci_trb *trb)
{
  return trb->status >> XHCI_TRB_COMPLETION_SHIFT;
}

static struct xhci_trb *
xhci_ring_enqueue_with_cycle(struct xhci_ring *ring,
                             uint64 parameter,
                             uint32 status,
                             uint32 control,
                             uint32 trb_cycle)
{
  uint32 usable;
  struct xhci_trb *trb;
  struct xhci_trb *link;

  usable = ring->count - (ring->has_link ? 1U : 0U);

  if(ring->enqueue >= usable)
    return 0;

  trb = &ring->trbs[ring->enqueue];

  trb->parameter = parameter;
  trb->status = status;
  trb->control = (control & ~XHCI_TRB_CYCLE) |
                 (trb_cycle ? XHCI_TRB_CYCLE : 0U);

  usb_dma_sync_for_device(trb, sizeof(*trb));

  ring->enqueue++;

  if(ring->has_link && ring->enqueue == usable){
    link = &ring->trbs[usable];

    link->parameter = usb_dma_address(ring->trbs);
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
                    XHCI_TRB_TC |
                    (ring->producer_cycle ?
                     XHCI_TRB_CYCLE : 0U);

    usb_dma_sync_for_device(link, sizeof(*link));

    ring->enqueue = 0;
    ring->producer_cycle ^= 1U;
  }

  return trb;
}

static struct xhci_trb *
xhci_ring_enqueue(struct xhci_ring *ring,
                  uint64 parameter,
                  uint32 status,
                  uint32 control)
{
  return xhci_ring_enqueue_with_cycle(ring,
                                      parameter,
                                      status,
                                      control,
                                      ring->producer_cycle);
}

static int
xhci_event_pop(struct xhci_trb *event)
{
  struct xhci_ring *ring;
  struct xhci_trb *source;
  uint32 cycle;
  uint64 dequeue;

  ring = &xhci.event_ring;
  source = &ring->trbs[ring->dequeue];

  usb_dma_sync_for_cpu(source, sizeof(*source));
  cycle = (source->control & XHCI_TRB_CYCLE) != 0;

  if(cycle != ring->consumer_cycle)
    return 0;

  *event = *source;
  ring->dequeue++;

  if(ring->dequeue == ring->count){
    ring->dequeue = 0;
    ring->consumer_cycle ^= 1U;
  }

  dequeue = usb_dma_address(&ring->trbs[ring->dequeue]);
  xhci_write64(xhci.rt_base + XHCI_INTR_BASE + XHCI_ERDP,
               dequeue | XHCI_ERDP_EHB);

  //Se limpian los indicadores de evento aunque las IRQ estén desactivadas
  xhci_write32(xhci.rt_base + XHCI_INTR_BASE + XHCI_IMAN, 1U);
  xhci_write32(xhci.op_base + XHCI_USBSTS, XHCI_STS_EINT);

  return 1;
}

static int
xhci_completion_ok(uint32 completion)
{
  return completion == XHCI_CC_SUCCESS ||
         completion == XHCI_CC_SHORT_PACKET;
}

static int
xhci_wait_event(uint32 wanted_type,
                uint32 wanted_slot,
                uint32 wanted_endpoint,
                uint64 wanted_parameter,
                struct xhci_trb *result,
                uint32 timeout_us)
{
  uint64 start;
  uint64 timeout_ticks;
  struct xhci_trb event;
  uint32 type;
  uint32 slot;
  uint32 endpoint;
  uint32 completion;

  start = r_time();
  timeout_ticks = (uint64)timeout_us * 4ULL;

  while((r_time() - start) < timeout_ticks){
    if(!xhci_event_pop(&event)){
      xhci_delay_us(1);
      continue;
    }

    type = xhci_trb_get_type(&event);
    slot = event.control >> XHCI_TRB_SLOT_SHIFT;
    endpoint = (event.control >> XHCI_TRB_EP_SHIFT) & 0x1fU;
    completion = xhci_trb_get_completion(&event);

    if(type == XHCI_TRB_PORT_EVENT){
      //El evento se consume, pero no se imprime dentro del timeout
      continue;
    }

    if(type != wanted_type)
      continue;

    if(wanted_slot != 0 && slot != wanted_slot)
      continue;

    if(wanted_endpoint != 0 && endpoint != wanted_endpoint)
      continue;

    if(wanted_parameter != 0 &&
       (event.parameter & ~0xfULL) != (wanted_parameter & ~0xfULL)){
      /*
      En EP0 solo existe una transferencia pendiente. Un error de la fase
      de datos puede impedir que el controlador llegue al Status Stage.
      Se devuelve ese error en vez de convertirlo en un timeout posterior.
      */
      if(wanted_type == XHCI_TRB_TRANSFER_EVENT &&
         !xhci_completion_ok(completion)){
        *result = event;
        return 0;
      }
      continue;
    }

    *result = event;
    return 0;
  }

  XHCI_DEBUG("xhci: timeout waiting event type=%d slot=%d ep=%d ptr=%p\n",
         (int)wanted_type,
         (int)wanted_slot,
         (int)wanted_endpoint,
         (void *)wanted_parameter);
  return -1;
}

static void
xhci_ring_command_doorbell(void)
{
  xhci_write32(xhci.db_base, 0);
}

static void
xhci_ring_endpoint_doorbell(uint32 slot_id, uint32 endpoint_id)
{
  xhci_write32(xhci.db_base + ((uint64)slot_id * 4U), endpoint_id);
}

static int
xhci_submit_command(uint64 parameter,
                    uint32 control,
                    struct xhci_trb *event)
{
  struct xhci_trb *command;
  uint32 completion;

  command = xhci_ring_enqueue(&xhci.command_ring,
                              parameter,
                              0,
                              control);
  if(command == 0)
    return -1;

  xhci_ring_command_doorbell();

  if(xhci_wait_event(XHCI_TRB_COMMAND_EVENT,
                     0,
                     0,
                     usb_dma_address(command),
                     event,
                     XHCI_EVENT_TIMEOUT_US) < 0)
    return -1;

  completion = xhci_trb_get_completion(event);
  if(completion != XHCI_CC_SUCCESS){
    XHCI_DEBUG("xhci: command failed type=%d cc=%d\n",
           (int)((control & XHCI_TRB_TYPE_MASK) >>
                 XHCI_TRB_TYPE_SHIFT),
           (int)completion);
    return -1;
  }

  return 0;
}

int
xhci_command_enable_slot(uint32 *slot_id)
{
  struct xhci_trb event;

  if(xhci_submit_command(0,
                         XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT),
                         &event) < 0)
    return -1;

  *slot_id = event.control >> XHCI_TRB_SLOT_SHIFT;

  if(*slot_id == 0){
    XHCI_DEBUG("xhci: Enable Slot returned slot zero\n");
    return -1;
  }

  XHCI_DEBUG("xhci: Enable Slot completed slot=%d\n", (int)*slot_id);
  return 0;
}

int
xhci_command_address_device(uint32 slot_id)
{
  struct xhci_device *device;
  struct xhci_trb event;
  uint32 input_size;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  input_size = 33U * xhci.context_size;
  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_submit_command(usb_dma_address(device->input_context),
                         XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) |
                         (slot_id << XHCI_TRB_SLOT_SHIFT),
                         &event) < 0)
    return -1;

  XHCI_DEBUG("xhci: Address Device completed slot=%d\n", (int)slot_id);
  return 0;
}

int
xhci_command_evaluate_context(uint32 slot_id)
{
  struct xhci_device *device;
  struct xhci_trb event;
  uint32 input_size;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  input_size = 33U * xhci.context_size;
  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_submit_command(usb_dma_address(device->input_context),
                         XHCI_TRB_TYPE(XHCI_TRB_EVALUATE_CTX) |
                         (slot_id << XHCI_TRB_SLOT_SHIFT),
                         &event) < 0)
    return -1;

  XHCI_DEBUG("xhci: Evaluate Context completed slot=%d\n", (int)slot_id);
  return 0;
}

int
xhci_command_configure_endpoint(uint32 slot_id)
{
  struct xhci_device *device;
  struct xhci_trb event;
  uint32 input_size;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  input_size = 33U * xhci.context_size;
  usb_dma_sync_for_device(device->input_context, input_size);

  if(xhci_submit_command(usb_dma_address(device->input_context),
                         XHCI_TRB_TYPE(XHCI_TRB_CONFIG_EP) |
                         (slot_id << XHCI_TRB_SLOT_SHIFT),
                         &event) < 0)
    return -1;

  XHCI_DEBUG("xhci: Configure Endpoint completed slot=%d\n", (int)slot_id);
  return 0;
}

static uint64
xhci_setup_parameter(uchar request_type,
                     uchar request,
                     uint16 value,
                     uint16 index,
                     uint16 length)
{
  uint64 parameter;

  parameter = (uint64)request_type;
  parameter |= (uint64)request << 8;
  parameter |= (uint64)value << 16;
  parameter |= (uint64)index << 32;
  parameter |= (uint64)length << 48;

  return parameter;
}

int
xhci_control_transfer(uint32 slot_id,
                      uchar request_type,
                      uchar request,
                      uint16 value,
                      uint16 index,
                      void *buffer,
                      uint16 length)
{
  struct xhci_device *device;
  struct xhci_ring *ring;
  struct xhci_trb event;
  struct xhci_trb *setup_trb;
  struct xhci_trb *status_trb;
  uint32 start_cycle;
  uint32 setup_control;
  uint32 data_control;
  uint32 status_control;
  uint32 completion;
  int direction_in;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  ring = &device->ep0_ring;
  direction_in = (request_type & USB_DIR_IN) != 0;

  /*
  El buffer se publica antes de entregar el TD al controlador.
  Para transferencias IN se invalida otra vez al terminar.
  */
  if(buffer != 0 && length > 0)
    usb_dma_sync_for_device(buffer, length);

  /*
  El Setup Stage utiliza datos inmediatos y contiene los ocho bytes
  de la petición USB dentro del propio TRB.
  */
  setup_control = XHCI_TRB_TYPE(XHCI_TRB_SETUP) |
                  XHCI_TRB_IDT;

  if(length == 0)
    setup_control |= XHCI_SETUP_TRT_NONE;
  else if(direction_in)
    setup_control |= XHCI_SETUP_TRT_IN;
  else
    setup_control |= XHCI_SETUP_TRT_OUT;

  /*
  El primer TRB se escribe con el Cycle Bit contrario. De este modo
  el xHCI no puede empezar a consumir el TD mientras todavía se están
  construyendo las fases Data y Status.
  */
  start_cycle = ring->producer_cycle;

  setup_trb =
    xhci_ring_enqueue_with_cycle(
      ring,
      xhci_setup_parameter(request_type,
                           request,
                           value,
                           index,
                           length),
      8U,
      setup_control,
      start_cycle ^ 1U);

  if(setup_trb == 0)
    return -1;

  /*
  Si existe una fase de datos se añade después del Setup Stage.
  */
  if(length > 0){
    data_control = XHCI_TRB_TYPE(XHCI_TRB_DATA);

    if(direction_in)
      data_control |= XHCI_TRB_DIR_IN |
                      XHCI_TRB_ISP;

    if(xhci_ring_enqueue(ring,
                         usb_dma_address(buffer),
                         length,
                         data_control) == 0)
      return -1;
  }

  /*
  El Status Stage utiliza la dirección contraria a la fase de datos.
  Una petición sin datos termina con un Status Stage IN.
  */
  status_control = XHCI_TRB_TYPE(XHCI_TRB_STATUS) |
                   XHCI_TRB_IOC;

  if(length == 0 || !direction_in)
    status_control |= XHCI_TRB_DIR_IN;

  status_trb = xhci_ring_enqueue(ring,
                                 0,
                                 0,
                                 status_control);
  if(status_trb == 0)
    return -1;

  /*
  Todas las fases del TD ya están escritas. Se entrega ahora el primer
  TRB al controlador restaurando su Cycle Bit real y solo después se
  toca el doorbell del endpoint cero.
  */
  setup_trb->control &= ~XHCI_TRB_CYCLE;

  if(start_cycle)
    setup_trb->control |= XHCI_TRB_CYCLE;

  usb_dma_sync_for_device(setup_trb, sizeof(*setup_trb));

  asm volatile("fence iorw, iorw" ::: "memory");

  xhci_ring_endpoint_doorbell(slot_id, 1);

  /*
  Un paquete corto en la fase de datos puede producir un evento adicional.
  La transferencia termina cuando llega el evento correspondiente al
  Status TRB.
  */
  if(xhci_wait_event(XHCI_TRB_TRANSFER_EVENT,
                     slot_id,
                     1,
                     usb_dma_address(status_trb),
                     &event,
                     XHCI_EVENT_TIMEOUT_US) < 0)
    return -1;

  completion = xhci_trb_get_completion(&event);
  if(!xhci_completion_ok(completion)){
    XHCI_DEBUG("xhci: control transfer failed req=0x%x cc=%d\n",
           request,
           (int)completion);
    return -1;
  }

  if(buffer != 0 && length > 0)
    usb_dma_sync_for_cpu(buffer, length);

  return 0;
}

int
xhci_queue_interrupt_in(uint32 slot_id,
                        uint32 endpoint_id,
                        void *buffer,
                        uint32 length)
{
  struct xhci_device *device;
  struct xhci_ring *ring;

  device = xhci_get_device(&xhci, slot_id);
  if(device == 0)
    return -1;

  ring = &device->interrupt_ring;
  usb_dma_sync_for_device(buffer, length);

  if(xhci_ring_enqueue(ring,
                       usb_dma_address(buffer),
                       length,
                       XHCI_TRB_TYPE(XHCI_TRB_NORMAL) |
                       XHCI_TRB_IOC |
                       XHCI_TRB_ISP) == 0)
    return -1;

  xhci_ring_endpoint_doorbell(slot_id, endpoint_id);
  return 0;
}

int
xhci_poll_keyboard_transfer(uint32 slot_id,
                            uint32 endpoint_id,
                            uint32 *actual_length)
{
  struct xhci_trb event;
  uint32 type;
  uint32 event_slot;
  uint32 event_endpoint;
  uint32 completion;
  uint32 residual;

  while(xhci_event_pop(&event)){
    type = xhci_trb_get_type(&event);

    if(type == XHCI_TRB_PORT_EVENT)
      continue;

    if(type != XHCI_TRB_TRANSFER_EVENT)
      continue;

    event_slot = event.control >> XHCI_TRB_SLOT_SHIFT;
    event_endpoint =
      (event.control >> XHCI_TRB_EP_SHIFT) & 0x1fU;

    if(event_slot != slot_id || event_endpoint != endpoint_id)
      continue;

    completion = xhci_trb_get_completion(&event);
    if(!xhci_completion_ok(completion)){
      XHCI_DEBUG("usb-kbd: transfer failed cc=%d\n", (int)completion);
      return -1;
    }

    residual = event.status & XHCI_TRB_LENGTH_MASK;

    if(residual > usb_keyboard.max_packet)
      residual = usb_keyboard.max_packet;

    *actual_length = usb_keyboard.max_packet - residual;
    return 1;
  }

  return 0;
}
