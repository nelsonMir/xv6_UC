#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern unsigned char initcode[];
extern unsigned int  initcode_len;


struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

//NUEVO el array de estados para el comando ps, declarado en proc.h
char *states[] = {
  "UNUSED",   // Estado 0
  "USED",     // Estado 1
  "SLEEPING", // Estado 2
  "RUNNABLE", // Estado 3
  "RUNNING",  // Estado 4
  "ZOMBIE"    // Estado 5
};

//variable global para seleccionar el tipo de planificador a usar y por defecto RR
int scheduler_policy = 1; // 0: RR, 1: FCFS, 2: Lottery


// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
    uint64 va = KSTACK((int)(p - proc));

    // Reserva 2 páginas contiguas para la pila
    char *pa0 = kalloc();
    char *pa1 = kalloc();
    if(pa0 == 0 || pa1 == 0)
      panic("kstack kalloc");

    // Mapea las 2 páginas de pila
    kvmmap(kpgtbl, va,           (uint64)pa0, PGSIZE, PTE_R|PTE_W|PTE_A|PTE_D);
    kvmmap(kpgtbl, va + PGSIZE,  (uint64)pa1, PGSIZE, PTE_R|PTE_W|PTE_A|PTE_D);

    // La página siguiente (va + 2*PGSIZE) es la guardia y NO se mapea.
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  //anhado campo de prioridad
  p->priority = 0; //prioridad por defecto 0
  //anhado campo de tiempo de creacion para el FCFS
  p->creation_time = ticks;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  //printf("Tiempo de creacion proceso %d: %d\n", p->pid, p->creation_time);
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // PRODUCCIÓN: TRAMPOLINE/TRAPFRAME SIN PTE_U (solo supervisor).
  // En depuración NO añadas PTE_U aquí, o S-mode no podrá acceder con SUM=0.
  int trampperm = PTE_R | PTE_X | PTE_A | PTE_D;  // <- sin PTE_U
  int tfperm    = PTE_R | PTE_W | PTE_A | PTE_D;  // <- sin PTE_U

  // OJO con el PA que uso para “trampoline”:
  // (uint64)trampoline debe ser una PA válida. Si en tu port no es identidad,
  // usa kvmpa((uint64)trampoline) para obtener la PA real del código del trampolín.
  extern char trampoline[];
  uint64 tramp_pa = (uint64)trampoline;
  // tramp_pa = kvmpa((uint64)trampoline); // <- usa esto si tu kernel no es identidad VA=PA

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, tramp_pa, trampperm) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // TRAPFRAME debe apuntar a la PA de la página que kalloc() te dio.
  // Si p->trapframe fuese una VA de kernel (no PA), usa kvmpa().
  uint64 tf_pa = (uint64)p->trapframe;
  // tf_pa = kvmpa((uint64)p->trapframe); // <- si no tienes identidad VA=PA

  if (mappages(pagetable, TRAPFRAME, PGSIZE, tf_pa, tfperm) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}


// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}


void
userinit(void)
{
  printf("userinit: creando proceso init\r\n");
  struct proc *p;

  p = allocproc();
  initproc = p;

  extern unsigned int initcode_len;
  // extern char trampoline[];   // ← ya no es necesario aquí
  printf("DEBUG initcode_len=%u\r\n", initcode_len);

  // *** OJO ***
  // No mapear TRAMPOLINE/TRAPFRAME aquí: parece que allocproc()/proc_pagetable()
  // ya los mapea en el pagetable de USUARIO (por eso salta "remap" si lo repetimos).

  extern char trampoline[];   // símbolo del trampolín

  // Asegurar TRAMPOLINE en el pagetable de USUARIO (supervisor-only: sin U)
  pte_t *pte = walk(p->pagetable, TRAMPOLINE, 0);
  if(pte == 0 || (*pte & PTE_V) == 0){
    if(mappages(p->pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R|PTE_X) != 0)
      panic("userinit: map TRAMPOLINE");
  }

  // Asegurar TRAPFRAME en el pagetable de USUARIO (supervisor-only: sin U)
  pte = walk(p->pagetable, TRAPFRAME, 0);
  if(pte == 0 || (*pte & PTE_V) == 0){
    if(mappages(p->pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe, PTE_R|PTE_W) != 0)
      panic("userinit: map TRAPFRAME");
  }
  // Cargar initcode en la dirección virtual 0
  uvmfirst(p->pagetable, initcode, initcode_len);

  // Mapear una página más para el stack
  // ⚠️ Añadir PTE_U (y PTE_R) para que la vea el modo usuario
  if (uvmalloc(p->pagetable, PGSIZE, 2*PGSIZE, PTE_R | PTE_W | PTE_U) == 0)
    panic("userinit: uvmalloc");

  p->sz = 2 * PGSIZE;

  // EPC apunta al inicio del código
  p->trapframe->epc = 0;

  // --- Parche de diagnóstico: forzar U en la PTE de la pila ---
  pte_t *stkpte = walk(p->pagetable, PGSIZE, 0);   // VA 0x1000
  if(stkpte == 0)
    panic("userinit: no hay PTE para la pila (0x1000)");

  uint64 oldpte = *stkpte;
  *stkpte |= (PTE_U | PTE_R);   // Asegura U y R
  sfence_vma();                 // Asegura que la TLB vea el cambio

  printf("DEBUG stack PTE old=0x%lx new=0x%lx\r\n", oldpte, *stkpte);
  // --- fin parche ---

  // SP al final de la segunda página
  p->trapframe->sp = 2 * PGSIZE;

  // (Opcional) Comprobación inmediata
  // uint64 spa = walkaddr(p->pagetable, p->trapframe->sp - 16);
  // printf("DEBUG stack walk: pa=0x%lx\r\n", spa);

  printf("initcode cargado, epc=%ld sz=%ld\n", p->trapframe->epc, p->sz);

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  release(&p->lock);
}





// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np; //child process
  struct proc *p = myproc(); //father

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  //heredar la prioridad del padre
  np->priority = p->priority;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  //struct proc *p;
  struct cpu *c = mycpu();

  //agrego latidos para ver si esta vivo 
  int beat = 0;
  c->proc = 0;
  for(;;){

    // Sonda: recoge RX aunque no haya IRQs ni timer
    //uart_debug_poll();
    // Fallback: intenta chupar del UART en cada iteración
    // (esto es barato; solo lee si hay LSR_RX_READY)
    //uartintr();

    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    
    //lo nuevo, escoger el planificador
    switch(scheduler_policy){
      case 0:
        //como p y c son punteros, se mandan sin mas
        found = schedule_round_robin(c);
        break;

      case 1:
        found = schedule_fcfs(c);
        break;

      default:
        found = schedule_round_robin(c);
        break;
    }

    if(found == 0) {
       // Fallback: chupar del UART por polling si no hay IRQs
      uartintr();              // ← lee RHR si LSR_RX_READY
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }

    if((++beat & 0x3FFFF) == 0){
    printf("scheduler: tick\r\n");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
