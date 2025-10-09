#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

//para depurar 
// --- control de verbosidad de traps/PLIC ---
#ifndef VERBOSE_TRAP
#define VERBOSE_TRAP 0
#endif

#ifndef VERBOSE_PLIC
#define VERBOSE_PLIC 0
#endif

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

static inline void sbi_set_timer(uint64 when){
  register uint64 a0 asm("a0") = when;
  register uint64 a7 asm("a7") = 0x0UL; // legacy set_timer
  asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static inline uint64 rdtime_safe(void){
  uint64 t; asm volatile("rdtime %0":"=r"(t)); return t;
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);

  // Habilitar interrupciones de S-mode:
  //  - SEIE: external
  //  - STIE: timer
  //  - SSIE: software
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

   w_sscratch(0);                // ← esencial para el prologo de kernelvec

   sbi_set_timer(rdtime_safe() + 1000000); 
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  #if VERBOSE_TRAP
  printf("usertrap: scause=0x%lx sepc=0x%lx stval=0x%lx\r\n", r_scause(), r_sepc(), r_stval());
#endif

  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\r\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\r\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  intr_off();

  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  w_sepc(p->trapframe->epc);

  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}



// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);

      // --- TEMP: escanea RX por polling para que la consola responda
    //uart_debug_poll();
  }

  //uart_debug_poll();
  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  sbi_set_timer(rdtime_safe() + 1000000);

}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr(void)
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) && ((scause & 0xff) == 9)) {
    // Interrupción externa (vía PLIC)
    int irq = plic_claim();
    if (irq) {
      #if VERBOSE_PLIC
      printf("PLIC claim id=%d\n", irq);
      #endif

      if (irq == UART0_IRQ) {
        uartintr();
      } else if (irq == VIRTIO0_IRQ) {
        virtio_disk_intr();
      }  else {
         #if VERBOSE_PLIC
         printf("unexpected PLIC irq %d\n", irq);
         #endif
       }

      plic_complete(irq);
      return 1;
    }
    return 0;

  } else if (scause == 0x8000000000000005UL) {  // S-mode timer interrupt
  clockintr();
  return 2;
} else {
    return 0;
  }
}


