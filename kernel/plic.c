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
  // Pon prioridad 1 (≠ 0) a un rango razonable de IRQs.
  // Así no se quedan silenciados por prioridad=0.
  for (int irq = 1; irq < 64; irq++)
    *(volatile uint32 *)(PLIC_PRIORITY + irq*4) = 1;

  printf("plicinit done\n");
}

// Habilita un IRQ en el PLIC para este hart en S-mode (maneja ids > 31)
static inline void plic_enable_irq(int hart, int irq)
{
  volatile uint32 *en = (uint32*)PLIC_SENABLE(hart);
  int word = irq / 32;
  int bit  = irq % 32;
  en[word] |= (1u << bit);
}


// arriba del archivo:
extern void uart_enable_irq_runtime(void);

void plicinithart(void)
{
  int hart = r_tp();

  // Umbral a 0: permite pasar cualquier prioridad > 0
  *(uint32*)PLIC_SPRIORITY(hart) = 0;

  // Habilita el IRQ del UART y (si usas) virtio
  plic_enable_irq(hart, UART0_IRQ);
  plic_enable_irq(hart, VIRTIO0_IRQ);

  // Habilita interrupciones externas en S-mode
  w_sie(r_sie() | SIE_SEIE);
}



// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  return *(volatile uint32*)PLIC_SCLAIM(hart);
}
// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(volatile uint32*)PLIC_SCLAIM(hart) = irq;
}
