#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

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
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  printf("usertrap: scause=0x%lx sepc=0x%lx stval=0x%lx\r\n", r_scause(), r_sepc(), r_stval());

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

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  ///DEBUG: imprime justo antes de saltar a userret (cambiar a modo usuario)
printf("usertrapret: satp=0x%lx sepc=0x%lx sp=0x%lx pid=%d\r\n",
       satp, p->trapframe->epc, p->trapframe->sp, p->pid);

// TRAMPOLINE/TRAPFRAME: kernel pagetable
uint64 pa_tramp = kvmpa(TRAMPOLINE);
uint64 pa_tf    = kvmpa((uint64)p->trapframe);

// USER: texto OK
uint64 pa_text0 = walkaddr_reason(p->pagetable, 0);

// USER: pila — evitar borde superior
uint64 sp  = p->trapframe->sp;        // 0x2000
uint64 sva = PGROUNDDOWN(sp - 1);     // 0x1000
// Prueba explícita con la VA exacta de la página de pila
uint64 pa_stack = walkaddr_reason(p->pagetable, sva);

// (extra) chequeo directo a 0x1000 por si sva cambia
uint64 pa_stack_1000 = walkaddr_reason(p->pagetable, PGSIZE);

printf("mapchk: trampPA=0x%lx trapframePA=0x%lx text0PA=0x%lx stackPA=0x%lx (sva=0x%lx) stackPA@0x1000=0x%lx\r\n",
       pa_tramp, pa_tf, pa_text0, pa_stack, sva, pa_stack_1000);

  //mas prints de DEBUG 
  pte_t *utrp = walk(p->pagetable, TRAMPOLINE, 0);
pte_t *utf  = walk(p->pagetable, TRAPFRAME, 0);
printf("user PTE TRAMP: %s pte=0x%lx [V=%d U=%d R=%d W=%d X=%d]\r\n",
       utrp?"ok":"NULL", utrp?*utrp:0,
       utrp?!!(*utrp&PTE_V):0, utrp?!!(*utrp&PTE_U):0,
       utrp?!!(*utrp&PTE_R):0, utrp?!!(*utrp&PTE_W):0, utrp?!!(*utrp&PTE_X):0);
printf("user PTE TF   : %s pte=0x%lx [V=%d U=%d R=%d W=%d X=%d]\r\n",
       utf?"ok":"NULL", utf?*utf:0,
       utf?!!(*utf&PTE_V):0, utf?!!(*utf&PTE_U):0,
       utf?!!(*utf&PTE_R):0, utf?!!(*utf&PTE_W):0, utf?!!(*utf&PTE_X):0);


  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
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
    uart_debug_poll();
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
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
      printf("PLIC claim id=%d\n", irq);

      if (irq == UART0_IRQ) {
        uartintr();
      } else if (irq == VIRTIO0_IRQ) {
        virtio_disk_intr();
      } else if (uart_rx_ready()) {
        // Heurística: si hay RX listo ¡es el UART!
        printf("IRQ %d tiene RX_READY=1; lo trato como UART.\n", irq);
        uartintr();
      } else {
        printf("unexpected PLIC irq %d\n", irq);
      }

      plic_complete(irq);
      return 1;
    }
    return 0;

  } else if (scause == 0x8000000000000001L) {
    // Timer
    clockintr();
    return 2;

  } else {
    return 0;
  }
}


