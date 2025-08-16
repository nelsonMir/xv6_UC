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
void kernelvec();
extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  w_sscratch(0);
}

extern void clockintr(void);

void
kerneltrap(void)
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    which_dev = devintr();
  } else if(scause == 0x8000000000000001L){
    clockintr();
    which_dev = 2;
  } else {
    printf("scause=0x%lx\n", scause);
    printf("sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
usertrap(void)
{
  struct proc *p = myproc();

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  w_stvec((uint64)kernelvec);

  p->trapframe->kernel_satp = r_satp();
  p->trapframe->kernel_sp = p->kstack + KSTACK_SIZE;
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();

  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) && (scause & 0xff) == 9){
    if(devintr() == 0)
      ;
  } else if(scause == 8){
    if(killed(p))
      exit(-1);
    p->trapframe->epc += 4;
    intr_on();
    syscall();
  } else if(scause == 13 || scause == 15){
    printf("usertrap(): pagefault sepc=0x%lx va=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  } else {
    printf("usertrap(): unexpected scause=0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  usertrapret();
}

void
usertrapret(void)
{
  struct proc *p = myproc();

  intr_off();
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  p->trapframe->kernel_satp = r_satp();
  p->trapframe->kernel_sp = p->kstack + KSTACK_SIZE;
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();

  uint64 x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);

  w_sepc(p->trapframe->epc);

  uint64 satp = MAKE_SATP(p->pagetable);

  ((void (*)(uint64,uint64)) (TRAMPOLINE + (userret - trampoline)))(TRAPFRAME, satp);
}

int
devintr(void)
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) && ((scause & 0xff) == 9)) {
    int irq = plic_claim();
    if (irq) {
      if (irq == UART0_IRQ) {
        uartintr();
      } else if (irq == VIRTIO0_IRQ) {
        virtio_disk_intr();
      } else {
        printf("unexpected PLIC irq %d\n", irq);
      }
      plic_complete(irq);
      return 1;
    }
    return 0;

  } else if (scause == 0x8000000000000001L) {
    clockintr();
    return 2;

  } else {
    return 0;
  }
}

// Stub mínimo si no tienes driver de timer real.
void
clockintr(void)
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}
