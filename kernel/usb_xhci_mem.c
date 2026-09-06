
/*
Basado en xhci-mem.c de U-Boot y en el controlador xHCI de Linux.

*/

/*
usb_xhci_mem.c

Gestión mínima de memoria DMA para el VIA VL805.

Se utiliza una única zona estática porque xv6 no dispone de la capa DMA de
Linux o U-Boot. Todas las estructuras reciben una dirección física idéntica
a su dirección del kernel, como ocurre en el mapeo directo de este port.
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "usb_xhci.h"

//flag debug 
#ifndef DBG_XHCI
#define DBG_XHCI 0
#endif

#define XHCI_DEBUG(...)            \
  do {                             \
    if(DBG_XHCI)                   \
      printf(__VA_ARGS__);         \
  } while(0)


#define CCACHE_FLUSH64_OFFSET 0x0200U
#define USB_DMA_64K_BOUNDARY  65536U

static uchar usb_dma_area[USB_DMA_AREA_SIZE]
  __attribute__((aligned(USB_XHCI_PAGE_SIZE)));

static uint32 usb_dma_used;

static uint32
usb_align_up(uint32 value, uint32 alignment)
{
  return (value + alignment - 1U) & ~(alignment - 1U);
}

void
usb_dma_reset(void)
{
  memset(usb_dma_area, 0, sizeof(usb_dma_area));
  usb_dma_used = 0;
  usb_dma_sync_for_device(usb_dma_area, sizeof(usb_dma_area));
}

void *
usb_dma_alloc(uint32 size, uint32 alignment)
{
  uint32 start;
  void *pointer;

  if(alignment == 0 || (alignment & (alignment - 1U)) != 0){
    XHCI_DEBUG("usb-dma: invalid alignment=%d\n", (int)alignment);
    return 0;
  }

  start = usb_align_up(usb_dma_used, alignment);

  /*
  Un único TRB de transferencia no puede describir un buffer que cruce una
  frontera de 64 KiB. La regla también es segura para rings y contextos.
  */
  if(size <= USB_DMA_64K_BOUNDARY &&
     (start & (USB_DMA_64K_BOUNDARY - 1U)) + size >
       USB_DMA_64K_BOUNDARY)
    start = usb_align_up(start, USB_DMA_64K_BOUNDARY);

  if(start > USB_DMA_AREA_SIZE || size > USB_DMA_AREA_SIZE - start){
    XHCI_DEBUG("usb-dma: area exhausted used=%d request=%d\n",
           (int)usb_dma_used,
           (int)size);
    return 0;
  }

  pointer = &usb_dma_area[start];
  memset(pointer, 0, size);
  usb_dma_used = start + size;

  usb_dma_sync_for_device(pointer, size);
  return pointer;
}

uint64
usb_dma_address(const void *pointer)
{
  return (uint64)pointer;
}

/*
El JH7110 utilizado por este port dispone del registro FLUSH64 en el
controlador CCACHE. El mismo mecanismo ya se utiliza para el framebuffer
HDMI. Se recorre el rango por líneas de 64 bytes antes de ceder la memoria
al xHCI y antes de volver a leer memoria escrita mediante DMA.

En la ruta PCIe del JH7110 la coherencia puede depender de la conexión del
dispositivo al interconnect. Mantener esta operación hace la primera versión
conservadora y evita depender de esa propiedad durante las pruebas.
*/
static void
usb_dma_flush_range(const void *pointer, uint32 size)
{
  uint64 start;
  uint64 end;
  uint64 line;

  if(pointer == 0 || size == 0)
    return;

  start = usb_dma_address(pointer) &
          ~((uint64)USB_DMA_CACHE_LINE - 1U);

  end = (usb_dma_address(pointer) + size +
         USB_DMA_CACHE_LINE - 1U) &
        ~((uint64)USB_DMA_CACHE_LINE - 1U);

  asm volatile("fence iorw, iorw" ::: "memory");

  for(line = start; line < end; line += USB_DMA_CACHE_LINE)
    *(volatile uint64 *)(CCACHE_BASE + CCACHE_FLUSH64_OFFSET) = line;

  asm volatile("fence iorw, iorw" ::: "memory");
}

void
usb_dma_sync_for_device(const void *pointer, uint32 size)
{
  usb_dma_flush_range(pointer, size);
}

void
usb_dma_sync_for_cpu(const void *pointer, uint32 size)
{
  usb_dma_flush_range(pointer, size);
}

static int
xhci_ring_allocate(struct xhci_ring *ring, int has_link)
{
  uint32 usable;
  struct xhci_trb *link;

  memset(ring, 0, sizeof(*ring));

  ring->trbs = usb_dma_alloc(USB_XHCI_PAGE_SIZE,
                             USB_XHCI_PAGE_SIZE);
  if(ring->trbs == 0)
    return -1;

  ring->count = USB_XHCI_PAGE_SIZE / sizeof(struct xhci_trb);
  ring->producer_cycle = 1;
  ring->consumer_cycle = 1;
  ring->has_link = has_link;

  if(has_link){
    usable = ring->count - 1U;
    link = &ring->trbs[usable];
    link->parameter = usb_dma_address(ring->trbs);
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
                    XHCI_TRB_TC |
                    XHCI_TRB_CYCLE;
    usb_dma_sync_for_device(link, sizeof(*link));
  }

  return 0;
}

static uint32
xhci_scratchpad_count(uint32 hcsparams2)
{
  uint32 low;
  uint32 high;

  low = (hcsparams2 >> 27) & 0x1fU;
  high = (hcsparams2 >> 21) & 0x1fU;

  return (high << 5) | low;
}

int
xhci_mem_init(struct xhci_controller *controller)
{
  uint32 hcsparams2;
  uint32 index;
  void *scratchpad;
  uint64 interrupter;

  usb_dma_reset();

  controller->dcbaa = usb_dma_alloc(256U * sizeof(uint64), 64U);
  if(controller->dcbaa == 0)
    return -1;

  if(xhci_ring_allocate(&controller->command_ring, 1) < 0)
    return -1;

  if(xhci_ring_allocate(&controller->event_ring, 0) < 0)
    return -1;

  controller->erst = usb_dma_alloc(sizeof(struct xhci_erst_entry), 64U);
  if(controller->erst == 0)
    return -1;

  controller->erst[0].address =
    usb_dma_address(controller->event_ring.trbs);
  controller->erst[0].size = controller->event_ring.count;
  controller->erst[0].reserved = 0;

  hcsparams2 = xhci_read32(controller->cap_base + XHCI_HCSPARAMS2);
  controller->scratchpad_count = xhci_scratchpad_count(hcsparams2);

  XHCI_DEBUG("xhci: HCSPARAMS2=0x%lx scratchpads=%d page=%d\n",
         (uint64)hcsparams2,
         (int)controller->scratchpad_count,
         (int)controller->page_size);

  if(controller->scratchpad_count > 0){
    controller->scratchpad_array =
      usb_dma_alloc(controller->scratchpad_count * sizeof(uint64), 64U);

    if(controller->scratchpad_array == 0)
      return -1;

    for(index = 0; index < controller->scratchpad_count; index++){
      scratchpad = usb_dma_alloc(controller->page_size,
                                 controller->page_size);
      if(scratchpad == 0)
        return -1;

      controller->scratchpad_array[index] =
        usb_dma_address(scratchpad);
    }

    controller->dcbaa[0] =
      usb_dma_address(controller->scratchpad_array);

    usb_dma_sync_for_device(
      controller->scratchpad_array,
      controller->scratchpad_count * sizeof(uint64));
  }

  usb_dma_sync_for_device(controller->dcbaa,
                          256U * sizeof(uint64));
  usb_dma_sync_for_device(controller->erst,
                          sizeof(struct xhci_erst_entry));

  xhci_write64(controller->op_base + XHCI_DCBAAP,
               usb_dma_address(controller->dcbaa));

  xhci_write64(controller->op_base + XHCI_CRCR,
               usb_dma_address(controller->command_ring.trbs) |
               controller->command_ring.producer_cycle);

  xhci_write32(controller->op_base + XHCI_DNCTRL, 0);

  interrupter = controller->rt_base + XHCI_INTR_BASE;
  xhci_write32(interrupter + XHCI_ERSTSZ, 1);
  xhci_write64(interrupter + XHCI_ERSTBA,
               usb_dma_address(controller->erst));
  xhci_write64(interrupter + XHCI_ERDP,
               usb_dma_address(controller->event_ring.trbs));
  xhci_write32(interrupter + XHCI_IMOD, 0);

  XHCI_DEBUG("xhci: DMA area=%p used=%d\n",
         (void *)usb_dma_area,
         (int)usb_dma_used);
  XHCI_DEBUG("xhci: DCBAA=%p command=%p event=%p\n",
         (void *)controller->dcbaa,
         (void *)controller->command_ring.trbs,
         (void *)controller->event_ring.trbs);
  XHCI_DEBUG("xhci: scratchpads=%d\n",
         (int)controller->scratchpad_count);

  return 0;
}

struct xhci_device *
xhci_get_device(struct xhci_controller *controller, uint32 slot_id)
{
  if(slot_id == 0 || slot_id > USB_XHCI_MAX_SLOTS_USED)
    return 0;

  if(!controller->devices[slot_id].allocated)
    return 0;

  return &controller->devices[slot_id];
}

uint32 *
xhci_input_control(struct xhci_controller *controller,
                   struct xhci_device *device)
{
  (void)controller;
  return (uint32 *)device->input_context;
}

uint32 *
xhci_input_slot(struct xhci_controller *controller,
                struct xhci_device *device)
{
  return (uint32 *)(device->input_context +
                    controller->context_size);
}

uint32 *
xhci_input_endpoint(struct xhci_controller *controller,
                    struct xhci_device *device,
                    uint32 endpoint_id)
{
  return (uint32 *)(device->input_context +
                    ((uint64)(endpoint_id + 1U) *
                     controller->context_size));
}

uint32 *
xhci_output_slot(struct xhci_controller *controller,
                 struct xhci_device *device)
{
  (void)controller;
  return (uint32 *)device->output_context;
}

uint32 *
xhci_output_endpoint(struct xhci_controller *controller,
                     struct xhci_device *device,
                     uint32 endpoint_id)
{
  return (uint32 *)(device->output_context +
                    ((uint64)endpoint_id *
                     controller->context_size));
}

static uint32
xhci_default_ep0_packet(uint32 speed)
{
  if(speed == XHCI_SPEED_HIGH)
    return 64U;

  if(speed == XHCI_SPEED_SUPER)
    return 512U;

  return 8U;
}

int
xhci_alloc_device(struct xhci_controller *controller,
                  uint32 slot_id,
                  uint32 root_port_id,
                  uint32 speed,
                  uint32 route_string,
                  uint32 tt_hub_slot_id,
                  uint32 tt_port_id)
{
  struct xhci_device *device;
  uint32 output_size;
  uint32 input_size;
  uint32 *control;
  uint32 *slot;
  uint32 *ep0;
  uint32 packet;

  if(slot_id == 0 || slot_id > USB_XHCI_MAX_SLOTS_USED){
    XHCI_DEBUG("xhci: slot %d is outside the static device table\n",
           (int)slot_id);
    return -1;
  }

  device = &controller->devices[slot_id];
  memset(device, 0, sizeof(*device));

  output_size = 32U * controller->context_size;
  input_size = 33U * controller->context_size;

  device->output_context = usb_dma_alloc(output_size, 64U);
  device->input_context = usb_dma_alloc(input_size, 64U);

  if(device->output_context == 0 || device->input_context == 0)
    return -1;

  if(xhci_ring_allocate(&device->ep0_ring, 1) < 0)
    return -1;

  if(xhci_ring_allocate(&device->interrupt_ring, 1) < 0)
    return -1;

  device->allocated = 1;
  device->slot_id = slot_id;
  device->root_port_id = root_port_id;
  device->speed = speed;
  device->route_string = route_string & XHCI_SLOT_ROUTE_MASK;
  device->tt_hub_slot_id = tt_hub_slot_id;
  device->tt_port_id = tt_port_id;
  device->context_size = controller->context_size;

  packet = xhci_default_ep0_packet(speed);
  device->ep0_max_packet = packet;

  controller->dcbaa[slot_id] =
    usb_dma_address(device->output_context);
  usb_dma_sync_for_device(controller->dcbaa,
                          256U * sizeof(uint64));

  memset(device->input_context, 0, input_size);
  control = xhci_input_control(controller, device);
  slot = xhci_input_slot(controller, device);
  ep0 = xhci_input_endpoint(controller, device, 1);

  //Se añaden el Slot Context y el Endpoint Context cero
  control[1] = USB_BIT(0) | USB_BIT(1);

  slot[0] = device->route_string |
            (speed << XHCI_SLOT_SPEED_SHIFT) |
            (1U << XHCI_SLOT_ENTRIES_SHIFT);
  slot[1] = root_port_id << XHCI_SLOT_ROOT_PORT_SHIFT;

  /*
  Los campos TT solo se utilizan para un dispositivo Full/Low Speed situado
  detrás del hub High-Speed. Para el propio hub permanecen a cero.
  */
  if(tt_hub_slot_id != 0){
    slot[2] = (tt_hub_slot_id << XHCI_SLOT_TT_SLOT_SHIFT) |
              (tt_port_id << XHCI_SLOT_TT_PORT_SHIFT);
  }

  ep0[1] = (3U << XHCI_EP_CERR_SHIFT) |
           (XHCI_EP_TYPE_CONTROL << XHCI_EP_TYPE_SHIFT) |
           (packet << XHCI_EP_MAX_PACKET_SHIFT);
  ep0[2] = (uint32)(usb_dma_address(device->ep0_ring.trbs) | 1U);
  ep0[3] = (uint32)(usb_dma_address(device->ep0_ring.trbs) >> 32);
  ep0[4] = 8U;

  usb_dma_sync_for_device(device->input_context, input_size);
  usb_dma_sync_for_device(device->output_context, output_size);

  XHCI_DEBUG("xhci: slot %d device context=%p input=%p\n",
         (int)slot_id,
         (void *)device->output_context,
         (void *)device->input_context);
  XHCI_DEBUG("xhci: slot %d root=%d speed=%d route=0x%x tt-slot=%d tt-port=%d ep0=%d\n",
         (int)slot_id,
         (int)root_port_id,
         (int)speed,
         (int)device->route_string,
         (int)tt_hub_slot_id,
         (int)tt_port_id,
         (int)packet);

  return 0;
}
