#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "xhci.h"

static inline uint8
xhci_read8(uint64 offset)
{
  volatile uint8 *reg;

  reg = (volatile uint8 *)(VF2_USB_XHCI_BASE + offset);
  return *reg;
}

static inline uint16
xhci_read16(uint64 offset)
{
  volatile uint16 *reg;

  reg = (volatile uint16 *)(VF2_USB_XHCI_BASE + offset);
  return *reg;
}

static inline uint32
xhci_read32(uint64 offset)
{
  volatile uint32 *reg;

  reg = (volatile uint32 *)(VF2_USB_XHCI_BASE + offset);
  return *reg;
}

void
xhci_probe(void)
{
  uint8 caplength;
  uint16 version;
  uint32 hcsparams1;
  uint32 hcsparams2;
  uint32 hcsparams3;
  uint32 hccparams1;
  uint32 dboff;
  uint32 rtsoff;
  uint32 max_slots;
  uint32 max_intrs;
  uint32 max_ports;

  printf("xhci: probing Cadence host at %p\n",
         (void *)VF2_USB_XHCI_BASE);

  //Barrera antes de comenzar los accesos MMIO.

  __sync_synchronize();

  printf("xhci: reading CAPLENGTH\n");
  caplength = xhci_read8(XHCI_CAPLENGTH);
  printf("xhci: CAPLENGTH=%d\n", caplength);

  printf("xhci: reading HCIVERSION\n");
  version = xhci_read16(XHCI_HCIVERSION);
  printf("xhci: HCIVERSION=0x%x\n", version);

  printf("xhci: reading HCSPARAMS1\n");
  hcsparams1 = xhci_read32(XHCI_HCSPARAMS1);
  printf("xhci: HCSPARAMS1=0x%x\n", hcsparams1);

  printf("xhci: reading HCSPARAMS2\n");
  hcsparams2 = xhci_read32(XHCI_HCSPARAMS2);
  printf("xhci: HCSPARAMS2=0x%x\n", hcsparams2);

  printf("xhci: reading HCSPARAMS3\n");
  hcsparams3 = xhci_read32(XHCI_HCSPARAMS3);
  printf("xhci: HCSPARAMS3=0x%x\n", hcsparams3);

  printf("xhci: reading HCCPARAMS1\n");
  hccparams1 = xhci_read32(XHCI_HCCPARAMS1);
  printf("xhci: HCCPARAMS1=0x%x\n", hccparams1);

  printf("xhci: reading DBOFF\n");
  dboff = xhci_read32(XHCI_DBOFF);
  printf("xhci: DBOFF=0x%x\n", dboff);

  printf("xhci: reading RTSOFF\n");
  rtsoff = xhci_read32(XHCI_RTSOFF);
  printf("xhci: RTSOFF=0x%x\n", rtsoff);

  //Barrera después de las lecturas MMIO
  __sync_synchronize();

  max_slots = hcsparams1 & 0xff;
  max_intrs = (hcsparams1 >> 8) & 0x7ff;
  max_ports = (hcsparams1 >> 24) & 0xff;

  printf("xhci: max slots=%d\n", max_slots);
  printf("xhci: max interrupters=%d\n", max_intrs);
  printf("xhci: max ports=%d\n", max_ports);

  /*
   xHCI válida:
   
    - CAPLENGTH distinto de cero
    - CAPLENGTH distinto de 0xff
    - Al menos un puerto
   */
  if(caplength == 0 || caplength == 0xff){
    printf("xhci: controller not accessible; "
           "clocks/reset may be disabled\n");
    return;
  }

  if(max_ports == 0){
    printf("xhci: invalid number of ports\n");
    return;
  }

  printf("xhci: capability registers accessible\n");
}

