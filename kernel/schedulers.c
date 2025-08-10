#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct cpu cpus[NCPU];

extern struct proc proc[NPROC];

extern struct proc *initproc;

extern struct spinlock pid_lock;

extern void forkret(void);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
extern struct spinlock wait_lock;

int schedule_round_robin(struct cpu *c){
    struct proc *p;

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        printf("sched rr: run pid=%d (%s)\r\n", p->pid, p->name);

        swtch(&c->context, &p->context);

         // Hemos vuelto del proceso
        printf("sched rr: back pid=%d (%s) state=%d\r\n", p->pid, p->name, p->state);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1; //hemos encontrado un proceso RUNNABLE
      }
      release(&p->lock);
    }

    return found; //si no hemos encontrado ningun proceso RUNNABLE devolvemos 0
}

int schedule_fcfs(struct cpu *c)
{
  struct proc *p;

  struct proc *earliest = 0;

  // Buscar el proceso RUNNABLE con menor tiempo de creación
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == RUNNABLE){
      if(earliest == 0 || p->creation_time < earliest->creation_time){
        if(earliest)
          release(&earliest->lock);
        earliest = p;
      } else {
        release(&p->lock);
      }
    } else {
      release(&p->lock);
    }
  }

  if(earliest){
    earliest->state = RUNNING;
    c->proc = earliest;

    // Cambiar de contexto al proceso elegido
    swtch(&c->context, &earliest->context);

    // Volver aquí cuando el proceso termine o ceda la CPU
    c->proc = 0;
    release(&earliest->lock);
    
    return 1;
  }

  return 0;
}

