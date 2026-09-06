
/*
Basado en el controlador PLDA PCIe de StarFive para el JH7110.

*/

/*
vf2_pcie_vl805.c

Extensión de la etapa 1 de PCIe0.

La etapa 1 del usuario ya enciende PCIe0, configura el PHY, libera PERST y
comprueba DATA_LINK_ACTIVE. Este fichero comienza después de ese punto:

- programa las tablas ATR del PLDA;
- configura los buses 0 y 1 del Root Port;
- abre una ventana de memoria PCI;
- detecta 1106:3483 mediante ECAM;
- asigna BAR0 al VIA VL805;
- habilita Memory Space y Bus Master;
- entrega la base xHCI a usb_xhci.c.
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "vf2_pcie.h"
#include "usb_xhci.h"

//flag debug 
#ifndef DBG_PCIE
#define DBG_PCIE 0
#endif

#define PCIE_DEBUG(...)             \
  do {                             \
    if(DBG_PCIE)                    \
      printf(__VA_ARGS__);         \
  } while(0)


#define XR3PCI_ATR_AXI4_SLV0             0x0800U
#define XR3PCI_ATR_SRC_ADDR_LOW          0x00U
#define XR3PCI_ATR_SRC_ADDR_HIGH         0x04U
#define XR3PCI_ATR_TRSL_ADDR_LOW         0x08U
#define XR3PCI_ATR_TRSL_ADDR_HIGH        0x0cU
#define XR3PCI_ATR_TRSL_PARAM            0x10U
#define XR3PCI_ATR_TABLE_OFFSET          0x20U
#define XR3PCI_ATR_SRC_WIN_SIZE_SHIFT    1U
#define XR3PCI_ATR_SRC_ADDR_MASK         0xfffff000U
#define XR3PCI_ATR_TRSL_ADDR_MASK        0xfffff000U
#define XR3PCI_ATR_PCIE_MEMORY           0x0U
#define XR3PCI_ATR_PCIE_CONFIG           0x1U

#define PCI_ECAM_BUS_SHIFT               20U
#define PCI_ECAM_DEV_SHIFT               15U
#define PCI_ECAM_FUNC_SHIFT              12U

#define PCI_VENDOR_DEVICE                0x00U
#define PCI_COMMAND_STATUS               0x04U
#define PCI_CLASS_REVISION               0x08U
#define PCI_HEADER_TYPE                  0x0cU
#define PCI_BAR0                         0x10U
#define PCI_BAR1                         0x14U
#define PCI_BUS_NUMBERS                  0x18U
#define PCI_MEMORY_BASE_LIMIT            0x20U

#define PCI_COMMAND_MEMORY               (1U << 1)
#define PCI_COMMAND_MASTER               (1U << 2)
#define PCI_BAR_IO                       (1U << 0)
#define PCI_BAR_MEM_TYPE_MASK            (3U << 1)
#define PCI_BAR_MEM_TYPE_64              (2U << 1)
#define PCI_BAR_MEM_MASK                 0xfffffff0U

#define VIA_VENDOR_ID                    0x1106U
#define VL805_DEVICE_ID                  0x3483U
#define VL805_ASSIGNED_BAR               VF2_PCIE0_MEM_BASE

static uint64 vl805_bar;

static uint32
pcie_read32(uint64 address)
{
  uint32 value;

  asm volatile("fence iorw, iorw" ::: "memory");
  value = *(volatile uint32 *)address;
  asm volatile("fence iorw, iorw" ::: "memory");

  return value;
}

static void
pcie_write32(uint64 address, uint32 value)
{
  asm volatile("fence iorw, iorw" ::: "memory");
  *(volatile uint32 *)address = value;
  asm volatile("fence iorw, iorw" ::: "memory");
}

static uint32
pcie_fls64(uint64 value)
{
  uint32 bit;

  bit = 0;
  while(value != 0){
    value >>= 1;
    bit++;
  }

  return bit;
}

static void
vf2_pcie0_set_atr(uint32 table,
                  uint64 source,
                  uint64 translation,
                  uint64 size,
                  uint32 parameter)
{
  uint64 base;
  uint32 size_field;

  base = VF2_PCIE0_APB_BASE + XR3PCI_ATR_AXI4_SLV0 +
         ((uint64)table * XR3PCI_ATR_TABLE_OFFSET);

  size_field = pcie_fls64(size) - 1U;

  pcie_write32(base + XR3PCI_ATR_SRC_ADDR_LOW,
               ((uint32)source & XR3PCI_ATR_SRC_ADDR_MASK) |
               (size_field << XR3PCI_ATR_SRC_WIN_SIZE_SHIFT) |
               1U);
  pcie_write32(base + XR3PCI_ATR_SRC_ADDR_HIGH,
               (uint32)(source >> 32));
  pcie_write32(base + XR3PCI_ATR_TRSL_ADDR_LOW,
               (uint32)translation & XR3PCI_ATR_TRSL_ADDR_MASK);
  pcie_write32(base + XR3PCI_ATR_TRSL_ADDR_HIGH,
               (uint32)(translation >> 32));
  pcie_write32(base + XR3PCI_ATR_TRSL_PARAM, parameter);

  PCIE_DEBUG("pcie0: ATR%d source=%p target=%p size=%p param=%d\n",
         (int)table,
         (void *)source,
         (void *)translation,
         (void *)size,
         (int)parameter);
}

static uint64
vf2_pcie0_ecam(uint32 bus,
               uint32 device,
               uint32 function,
               uint32 offset)
{
  return VF2_PCIE0_CFG_BASE +
         ((uint64)bus << PCI_ECAM_BUS_SHIFT) +
         ((uint64)device << PCI_ECAM_DEV_SHIFT) +
         ((uint64)function << PCI_ECAM_FUNC_SHIFT) +
         (offset & ~3U);
}

static uint32
vf2_pcie0_config_read(uint32 bus,
                      uint32 device,
                      uint32 function,
                      uint32 offset)
{
  if(offset == PCI_VENDOR_DEVICE)
    xhci_delay_us(20000U);

  return pcie_read32(vf2_pcie0_ecam(bus,
                                    device,
                                    function,
                                    offset));
}

static void
vf2_pcie0_config_write(uint32 bus,
                       uint32 device,
                       uint32 function,
                       uint32 offset,
                       uint32 value)
{
  pcie_write32(vf2_pcie0_ecam(bus,
                              device,
                              function,
                              offset),
               value);
}

static void
vf2_pcie0_configure_root_bridge(void)
{
  uint32 buses;
  uint32 memory;
  uint32 command;
  uint32 memory_base;
  uint32 memory_limit;

  //Primary=0, Secondary=1 y Subordinate=1
  buses = 0U | (1U << 8) | (1U << 16);
  vf2_pcie0_config_write(0, 0, 0, PCI_BUS_NUMBERS, buses);

  memory_base = (VF2_PCIE0_MEM_BASE >> 16) & 0xfff0U;
  memory_limit =
    ((VF2_PCIE0_MEM_BASE + VF2_PCIE0_MEM_SIZE - 1U) >> 16) & 0xfff0U;
  memory = memory_base | (memory_limit << 16);
  vf2_pcie0_config_write(0, 0, 0, PCI_MEMORY_BASE_LIMIT, memory);

  command = vf2_pcie0_config_read(0, 0, 0, PCI_COMMAND_STATUS);
  command &= 0x0000ffffU;
  command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
  vf2_pcie0_config_write(0, 0, 0, PCI_COMMAND_STATUS, command);

  PCIE_DEBUG("pcie0: root buses=0x%x memory-window=0x%x\n",
         buses,
         memory);
}

static uint64
vf2_pcie0_probe_bar_size(int is_64)
{
  uint32 original_low;
  uint32 original_high;
  uint32 mask_low;
  uint32 mask_high;
  uint64 mask;

  original_low = vf2_pcie0_config_read(1, 0, 0, PCI_BAR0);
  original_high = vf2_pcie0_config_read(1, 0, 0, PCI_BAR1);

  vf2_pcie0_config_write(1, 0, 0, PCI_BAR0, 0xffffffffU);
  if(is_64)
    vf2_pcie0_config_write(1, 0, 0, PCI_BAR1, 0xffffffffU);

  mask_low = vf2_pcie0_config_read(1, 0, 0, PCI_BAR0);
  mask_high = is_64 ?
    vf2_pcie0_config_read(1, 0, 0, PCI_BAR1) : 0xffffffffU;

  vf2_pcie0_config_write(1, 0, 0, PCI_BAR0, original_low);
  vf2_pcie0_config_write(1, 0, 0, PCI_BAR1, original_high);

  mask = ((uint64)mask_high << 32) |
         (mask_low & PCI_BAR_MEM_MASK);

  if(mask == 0 || mask == 0xfffffffffffffff0ULL)
    return 0;

  return (~mask) + 1ULL;
}

static int
vf2_pcie0_assign_vl805_bar(void)
{
  uint32 bar_low;
  uint32 command;
  uint64 size;
  int is_64;

  bar_low = vf2_pcie0_config_read(1, 0, 0, PCI_BAR0);

  if(bar_low & PCI_BAR_IO){
    PCIE_DEBUG("pcie0: VL805 BAR0 unexpectedly uses I/O space\n");
    return -1;
  }

  is_64 = (bar_low & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64;
  size = vf2_pcie0_probe_bar_size(is_64);

  if(size == 0)
    size = VF2_VL805_MMIO_SIZE;

  if(size > VF2_PCIE0_MEM_SIZE ||
     (VL805_ASSIGNED_BAR & (size - 1ULL)) != 0){
    PCIE_DEBUG("pcie0: unsupported VL805 BAR size=%p\n", (void *)size);
    return -1;
  }

  vf2_pcie0_config_write(1, 0, 0, PCI_BAR0,
                         ((uint32)VL805_ASSIGNED_BAR & PCI_BAR_MEM_MASK) |
                         (bar_low & ~PCI_BAR_MEM_MASK));

  if(is_64)
    vf2_pcie0_config_write(1, 0, 0, PCI_BAR1,
                           (uint32)((uint64)VL805_ASSIGNED_BAR >> 32));

  command = vf2_pcie0_config_read(1, 0, 0, PCI_COMMAND_STATUS);
  command &= 0x0000ffffU;
  command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
  vf2_pcie0_config_write(1, 0, 0, PCI_COMMAND_STATUS, command);

  vl805_bar = VL805_ASSIGNED_BAR;

  PCIE_DEBUG("pcie0: VL805 BAR0=%p size=%p type=%s\n",
         (void *)vl805_bar,
         (void *)size,
         is_64 ? "64-bit" : "32-bit");
  PCIE_DEBUG("pcie0: VL805 command=0x%x\n",
         vf2_pcie0_config_read(1, 0, 0, PCI_COMMAND_STATUS) & 0xffffU);

  return 0;
}

int
vf2_pcie0_probe_vl805(void)
{
  uint32 id;
  uint32 class_revision;
  uint32 header;
  uint32 vendor;
  uint32 device;

  vl805_bar = 0;

  if(!vf2_pcie0_link_up()){
    PCIE_DEBUG("pcie0: stage 1 link is not active\n");
    return -1;
  }

  PCIE_DEBUG("\n");
  PCIE_DEBUG("========================================\n");
  PCIE_DEBUG(" JH7110 PCIE0 - VL805 DISCOVERY\n");
  PCIE_DEBUG("========================================\n");

  /*
  PCIe exige esperar al menos cien milisegundos después de salir del
  reset convencional y tener el enlace activo antes de enviar la primera
  petición de configuración al endpoint.
  */
  xhci_delay_us(100000U);

  vf2_pcie0_set_atr(0,
                    VF2_PCIE0_CFG_BASE,
                    0,
                    1ULL << 28,
                    XR3PCI_ATR_PCIE_CONFIG);

  vf2_pcie0_set_atr(1,
                    VF2_PCIE0_MEM_BASE,
                    VF2_PCIE0_MEM_BASE,
                    VF2_PCIE0_MEM_SIZE,
                    XR3PCI_ATR_PCIE_MEMORY);

  vf2_pcie0_configure_root_bridge();

  id = vf2_pcie0_config_read(1, 0, 0, PCI_VENDOR_DEVICE);
  vendor = id & 0xffffU;
  device = id >> 16;
  class_revision = vf2_pcie0_config_read(1, 0, 0, PCI_CLASS_REVISION);
  header = vf2_pcie0_config_read(1, 0, 0, PCI_HEADER_TYPE);

  PCIE_DEBUG("pcie0: bus1 dev0 id=0x%x vendor=0x%x device=0x%x\n",
         id,
         vendor,
         device);
  PCIE_DEBUG("pcie0: class-revision=0x%x header=0x%x\n",
         class_revision,
         header);

  if(vendor != VIA_VENDOR_ID || device != VL805_DEVICE_ID){
    PCIE_DEBUG("pcie0: VIA VL805 1106:3483 not found\n");
    return -1;
  }

  if(vf2_pcie0_assign_vl805_bar() < 0)
    return -1;

  PCIE_DEBUG("pcie0: VL805 ready for xHCI\n");
  PCIE_DEBUG("========================================\n");
  return 0;
}

uint64
vf2_pcie0_vl805_bar(void)
{
  return vl805_bar;
}

int
vf2_usb_keyboard_init(void)
{
  if(vf2_pcie0_init() < 0){
    PCIE_DEBUG("usb-kbd: PCIe0 stage 1 failed\n");
    return -1;
  }

  if(vf2_pcie0_probe_vl805() < 0){
    PCIE_DEBUG("usb-kbd: VL805 discovery failed\n");
    return -1;
  }

  if(xhci_min_init(vf2_pcie0_vl805_bar()) < 0){
    PCIE_DEBUG("usb-kbd: xHCI initialization failed\n");
    return -1;
  }

  return usb_kbd_present() ? 0 : -1;
}
