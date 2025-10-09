#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

// plic.c (o un header común)
static inline int plic_ctx_hart(void) {
  // VF2 / Michael: macros PLIC_S* usan (hart-1) => devolver 1-based
  return r_tp() + 1;  // r_tp() 0->hart0, 1->hart1 ... => 1->hart1, 2->hart2 ...
}

void
plicinit(void)
{
  // Prioridades mínimas solo a las fuentes que usamos
  *(volatile uint32 *)(PLIC_PRIORITY + UART0_IRQ*4)   = 2;
  *(volatile uint32 *)(PLIC_PRIORITY + VIRTIO0_IRQ*4) = 1;

  printf("plicinit done\n");
}

// Helpers para SENABLE del hart (sobreescribe el word indicado).
static inline void plic_enable_only(int hart, int irq)
{
  volatile uint32 *en = (uint32*)PLIC_SENABLE(hart);
  int word = irq / 32;
  int bit  = irq % 32;
  en[word] = (1u << bit);
  printf("PLIC_SENABLE: hart=%d addr=%p w=%d val=0x%x (habilitado irq %d)\n",
         hart, (void*)&en[word], word, en[word], irq);
}

static inline void plic_disable_all_first64(int hart)
{
  volatile uint32 *en = (uint32*)PLIC_SENABLE(hart);
  en[0] = 0;
  en[1] = 0;
}


// arriba del archivo:
void plicinithart(void)
{
  int hart = plic_ctx_hart();                  // 1-based para PLIC_S*
  printf("plicinithart: r_tp=%ld cpuid=%d\r\n", r_tp(), cpuid());
  *(uint32*)PLIC_SPRIORITY(hart) = 0;          // threshold 0
   plic_disable_all_first64(hart);
  // De momento NO habilitamos nada fijo aquí: dejará que uart autodetecte su ID.
  w_sie(r_sie() | SIE_SEIE);                   // enable external ints
}

int plic_claim(void)
{
  int hart = plic_ctx_hart();                  // 1-based
  return *(volatile uint32*)PLIC_SCLAIM(hart);
}

void plic_complete(int irq)
{
  int hart = plic_ctx_hart();                  // 1-based
  *(volatile uint32*)PLIC_SCLAIM(hart) = irq;
}

// Expuestos para que uart.c pueda usarlos en la autodetección.
void plic_disable_all_first64_extern(void) { plic_disable_all_first64(plic_ctx_hart()); }
void plic_enable_only_extern(int irq) { plic_enable_only(plic_ctx_hart(), irq); }


