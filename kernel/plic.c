#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

static inline void
plic_enable_irq(int hart, int irq)
{
  volatile uint32 *senable_base = (uint32*)PLIC_SENABLE(hart);
  int word = irq / 32;
  int bit  = irq % 32;
  senable_base[word] |= (1u << bit);
}

void
plicinithart(void)
{
  int hart = cpuid();

  // habilitar UART y VIRTIO para este hart en S-mode
  plic_enable_irq(hart, UART0_IRQ);
  plic_enable_irq(hart, VIRTIO0_IRQ);

  // umbral de prioridad a 0: acepta todo > 0
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
