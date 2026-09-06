
/*
Basado en xhci.c de U-Boot y en el controlador xHCI de Linux.

/*
usb_xhci.c

Inicialización mínima del xHCI contenido en el VIA VL805.

La dirección base no está hardcodeada. Se recibe desde vf2_pcie.c después
de detectar el dispositivo PCI y asignarle su BAR de memoria.
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

#define JH7110_TICKS_PER_US       4ULL
#define XHCI_HALT_TIMEOUT_US      16000U
#define XHCI_RESET_TIMEOUT_US     250000U
#define XHCI_START_TIMEOUT_US     100000U
#define XHCI_PORT_TIMEOUT_US      500000U

struct xhci_controller xhci;

uint32
xhci_read32(uint64 address)
{
  uint32 value;

  asm volatile("fence iorw, iorw" ::: "memory");
  value = *(volatile uint32 *)address;
  asm volatile("fence iorw, iorw" ::: "memory");

  return value;
}

void
xhci_write32(uint64 address, uint32 value)
{
  asm volatile("fence iorw, iorw" ::: "memory");
  *(volatile uint32 *)address = value;
  asm volatile("fence iorw, iorw" ::: "memory");
}

uint64
xhci_read64(uint64 address)
{
  uint32 high1;
  uint32 high2;
  uint32 low;

  do {
    high1 = xhci_read32(address + 4U);
    low = xhci_read32(address);
    high2 = xhci_read32(address + 4U);
  } while(high1 != high2);

  return ((uint64)high2 << 32) | low;
}

void
xhci_write64(uint64 address, uint64 value)
{
  xhci_write32(address, (uint32)value);
  xhci_write32(address + 4U, (uint32)(value >> 32));
}

void
xhci_delay_us(uint32 microseconds)
{
  uint64 start;
  uint64 ticks;

  start = r_time();
  ticks = (uint64)microseconds * JH7110_TICKS_PER_US;

  while((r_time() - start) < ticks)
    asm volatile("nop");
}

int
xhci_wait32(uint64 address,
            uint32 mask,
            uint32 expected,
            uint32 timeout_us)
{
  uint64 start;
  uint64 timeout_ticks;
  uint32 value;

  start = r_time();
  timeout_ticks = (uint64)timeout_us * JH7110_TICKS_PER_US;

  do {
    value = xhci_read32(address);

    if((value & mask) == expected)
      return 0;

    xhci_delay_us(1);
  } while((r_time() - start) < timeout_ticks);

  XHCI_DEBUG("xhci: timeout address=%p value=0x%x\n",
         (void *)address,
         value);
  XHCI_DEBUG("xhci: mask=0x%x expected=0x%x\n",
         mask,
         expected);

  return -1;
}

static uint64
xhci_port_address(uint32 port_id)
{
  return xhci.op_base + XHCI_PORT_BASE +
         ((uint64)(port_id - 1U) * XHCI_PORT_STRIDE);
}

static uint32
xhci_port_neutral(uint32 portsc)
{
  return portsc & (XHCI_PORT_RO | XHCI_PORT_RWS);
}

uint32
xhci_port_speed(uint32 port_id)
{
  uint32 portsc;

  portsc = xhci_read32(xhci_port_address(port_id));
  return (portsc & XHCI_PORT_SPEED_MASK) >>
         XHCI_PORT_SPEED_SHIFT;
}

static void
xhci_dump_port(uint32 port_id)
{
  uint32 portsc;
  uint32 speed;
  uint32 pls;

  portsc = xhci_read32(xhci_port_address(port_id));
  speed = (portsc & XHCI_PORT_SPEED_MASK) >>
          XHCI_PORT_SPEED_SHIFT;
  pls = (portsc & XHCI_PORT_PLS_MASK) >>
        XHCI_PORT_PLS_SHIFT;

  XHCI_DEBUG("xhci: PORTSC%d=0x%x connected=%d enabled=%d\n",
         (int)port_id,
         portsc,
         (portsc & XHCI_PORT_CCS) != 0,
         (portsc & XHCI_PORT_PED) != 0);
  XHCI_DEBUG("xhci: port %d power=%d speed=%d pls=%d\n",
         (int)port_id,
         (portsc & XHCI_PORT_PP) != 0,
         (int)speed,
         (int)pls);
}

static int
xhci_root_port_reset(uint32 port_id)
{
  uint64 address;
  uint32 portsc;
  uint32 value;
  uint32 speed;

  address = xhci_port_address(port_id);
  portsc = xhci_read32(address);

  if((portsc & XHCI_PORT_CCS) == 0)
    return -1;

  if((portsc & XHCI_PORT_PP) == 0){
    value = xhci_port_neutral(portsc) |
            XHCI_PORT_PP |
            (portsc & XHCI_PORT_CHANGE_BITS);
    xhci_write32(address, value);
    xhci_delay_us(20000U);
    portsc = xhci_read32(address);
  }

  speed = (portsc & XHCI_PORT_SPEED_MASK) >>
          XHCI_PORT_SPEED_SHIFT;

  /*
  Los dispositivos SuperSpeed suelen llegar ya a U0 y con PED activo.
  Los teclados normales aparecen por el companion USB2 y necesitan PR.
  */
  if(speed == XHCI_SPEED_SUPER && (portsc & XHCI_PORT_PED)){
    xhci_write32(address,
                 xhci_port_neutral(portsc) |
                 (portsc & XHCI_PORT_CHANGE_BITS));
    return 0;
  }

  value = xhci_port_neutral(portsc) |
          XHCI_PORT_PR |
          (portsc & XHCI_PORT_CHANGE_BITS);
  xhci_write32(address, value);

  if(xhci_wait32(address,
                 XHCI_PORT_PR,
                 0,
                 XHCI_PORT_TIMEOUT_US) < 0){
    XHCI_DEBUG("xhci: port %d reset bit did not clear\n", (int)port_id);
    return -1;
  }

  if(xhci_wait32(address,
                 XHCI_PORT_PED,
                 XHCI_PORT_PED,
                 XHCI_PORT_TIMEOUT_US) < 0){
    XHCI_DEBUG("xhci: port %d did not become enabled\n", (int)port_id);
    return -1;
  }

  portsc = xhci_read32(address);
  xhci_write32(address,
               xhci_port_neutral(portsc) |
               (portsc & XHCI_PORT_CHANGE_BITS));

  XHCI_DEBUG("xhci: root port %d reset completed\n", (int)port_id);
  return 0;
}

static int
xhci_halt_and_reset(void)
{
  uint32 command;
  uint32 status;

  if(xhci_wait32(xhci.op_base + XHCI_USBSTS,
                 XHCI_STS_CNR,
                 0,
                 XHCI_RESET_TIMEOUT_US) < 0)
    return -1;

  command = xhci_read32(xhci.op_base + XHCI_USBCMD);
  status = xhci_read32(xhci.op_base + XHCI_USBSTS);

  if((status & XHCI_STS_HCH) == 0){
    command &= ~XHCI_CMD_RUN;
    xhci_write32(xhci.op_base + XHCI_USBCMD, command);

    if(xhci_wait32(xhci.op_base + XHCI_USBSTS,
                   XHCI_STS_HCH,
                   XHCI_STS_HCH,
                   XHCI_HALT_TIMEOUT_US) < 0)
      return -1;
  }

  command = xhci_read32(xhci.op_base + XHCI_USBCMD);
  command |= XHCI_CMD_HCRST;
  xhci_write32(xhci.op_base + XHCI_USBCMD, command);

  if(xhci_wait32(xhci.op_base + XHCI_USBCMD,
                 XHCI_CMD_HCRST,
                 0,
                 XHCI_RESET_TIMEOUT_US) < 0)
    return -1;

  if(xhci_wait32(xhci.op_base + XHCI_USBSTS,
                 XHCI_STS_CNR,
                 0,
                 XHCI_RESET_TIMEOUT_US) < 0)
    return -1;

  status = xhci_read32(xhci.op_base + XHCI_USBSTS);

  if(status & (XHCI_STS_HSE | XHCI_STS_HCE)){
    XHCI_DEBUG("xhci: controller error after reset USBSTS=0x%x\n", status);
    return -1;
  }

  return 0;
}

static int
xhci_start_controller(void)
{
  uint32 command;

  command = xhci_read32(xhci.op_base + XHCI_USBCMD);
  command |= XHCI_CMD_RUN;
  xhci_write32(xhci.op_base + XHCI_USBCMD, command);

  if(xhci_wait32(xhci.op_base + XHCI_USBSTS,
                 XHCI_STS_HCH,
                 0,
                 XHCI_START_TIMEOUT_US) < 0)
    return -1;

  XHCI_DEBUG("xhci: controller running USBCMD=0x%x USBSTS=0x%x\n",
         xhci_read32(xhci.op_base + XHCI_USBCMD),
         xhci_read32(xhci.op_base + XHCI_USBSTS));

  return 0;
}

int
xhci_scan_root_ports(void)
{
  uint32 port_id;
  uint32 portsc;
  uint32 speed;

  for(port_id = 1; port_id <= xhci.max_ports; port_id++){
    xhci_dump_port(port_id);
    portsc = xhci_read32(xhci_port_address(port_id));

    if((portsc & XHCI_PORT_CCS) == 0)
      continue;

    XHCI_DEBUG("xhci: device detected on root port %d\n", (int)port_id);

    if(xhci_root_port_reset(port_id) < 0)
      continue;

    speed = xhci_port_speed(port_id);

    if(usb_enumerate_keyboard(port_id, speed) == 0){
      XHCI_DEBUG("xhci: HID Boot keyboard ready on port %d\n",
             (int)port_id);
      return 0;
    }

    XHCI_DEBUG("xhci: connected device on port %d is not usable\n",
           (int)port_id);
  }

  XHCI_DEBUG("xhci: no HID Boot keyboard found\n");
  return -1;
}

int
xhci_min_init(uint64 bar)
{
  uint32 cap0;
  uint32 caplength;
  uint32 version;
  uint32 hcsparams1;
  uint32 hccparams1;
  uint32 dboff;
  uint32 rtsoff;
  uint32 pagesize;
  uint32 slots_enabled;

  memset(&xhci, 0, sizeof(xhci));

  XHCI_DEBUG("\n");
  XHCI_DEBUG("========================================\n");
  XHCI_DEBUG(" VIA VL805 XHCI - MINIMAL KEYBOARD PATH\n");
  XHCI_DEBUG("========================================\n");
  XHCI_DEBUG("xhci: BAR=%p\n", (void *)bar);

  xhci.cap_base = bar;
  cap0 = xhci_read32(bar + XHCI_CAPLENGTH);
  caplength = cap0 & 0xffU;
  version = (cap0 >> 16) & 0xffffU;
  hcsparams1 = xhci_read32(bar + XHCI_HCSPARAMS1);
  hccparams1 = xhci_read32(bar + XHCI_HCCPARAMS1);
  dboff = xhci_read32(bar + XHCI_DBOFF) & ~0x3U;
  rtsoff = xhci_read32(bar + XHCI_RTSOFF) & ~0x1fU;

  xhci.op_base = bar + caplength;
  xhci.db_base = bar + dboff;
  xhci.rt_base = bar + rtsoff;
  xhci.max_slots = hcsparams1 & 0xffU;
  xhci.max_interrupters = (hcsparams1 >> 8) & 0x7ffU;
  xhci.max_ports = (hcsparams1 >> 24) & 0xffU;
  xhci.context_size = (hccparams1 & USB_BIT(2)) ? 64U : 32U;

  XHCI_DEBUG("xhci: CAPLENGTH=0x%x HCIVERSION=0x%x\n",
         caplength,
         version);
  XHCI_DEBUG("xhci: slots=%d interrupters=%d ports=%d ctx=%d\n",
         (int)xhci.max_slots,
         (int)xhci.max_interrupters,
         (int)xhci.max_ports,
         (int)xhci.context_size);
  XHCI_DEBUG("xhci: op=%p db=%p rt=%p\n",
         (void *)xhci.op_base,
         (void *)xhci.db_base,
         (void *)xhci.rt_base);

  if(caplength < 0x20U ||
     xhci.max_slots == 0 ||
     xhci.max_ports == 0){
    XHCI_DEBUG("xhci: invalid capability registers\n");
    return -1;
  }

  if(xhci_halt_and_reset() < 0){
    XHCI_DEBUG("xhci: halt/reset failed\n");
    return -1;
  }

  pagesize = xhci_read32(xhci.op_base + XHCI_PAGESIZE);
  if((pagesize & 1U) == 0){
    XHCI_DEBUG("xhci: controller does not support 4 KiB pages\n");
    return -1;
  }
  xhci.page_size = USB_XHCI_PAGE_SIZE;

  if(xhci_mem_init(&xhci) < 0){
    XHCI_DEBUG("xhci: DMA setup failed\n");
    return -1;
  }

  slots_enabled = xhci.max_slots;
  if(slots_enabled > USB_XHCI_MAX_SLOTS_USED)
    slots_enabled = USB_XHCI_MAX_SLOTS_USED;

  xhci_write32(xhci.op_base + XHCI_CONFIG, slots_enabled);

  if(xhci_start_controller() < 0){
    XHCI_DEBUG("xhci: start failed\n");
    return -1;
  }

  xhci.ready = 1;

  if(xhci_scan_root_ports() < 0)
    XHCI_DEBUG("xhci: controller ready but keyboard was not enumerated\n");

  XHCI_DEBUG("========================================\n");
  return 0;
}
