#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//new syscalls 
//return the free mem that a process has
extern uint64 free_mem(void); //implementation in kalloc.c

uint64 sys_freemem(void){

  return free_mem();
}

extern uint64 get_page_size(void); //implementation in kalloc.c

//return the size of pages
uint64 sys_pagesize(void){

  return get_page_size(); 
}


//como vamos a usar la variable global "proc" que es un array con todos los procesos
//del sistema, asi que debemos usar el extern. proc esta definido en proc.c
extern struct proc proc[NPROC];

extern char *states[];

//return all active processes
uint64 sys_ps(void){

  //pcb del proceso actual 
  struct proc *p;

  //imprimos: PID   STATE   NAME   (el \t se pone el tabulador)
  printf("PID\tPRIORITY\tNAME\tSTATE\n");

  //nos recorremos el array de procesos
  for(p = proc; p < &proc[NPROC]; p++){

    if (p->state == UNUSED) continue; //si el proceso no esta en uso, se ignora y se pasa al siguiente

    //imprimos el PID, estado y nombre del proceso. SI no ponto el doble \t pues se desalinea por algun motivo
    //asi que hazme caso, probe varias combinaciones y solo esta funciona bien
    printf("%d\t%d\t\t%s\t%s\n", p->pid, p->priority, p->name, states[p->state]);
  }

  return 0;
}


//return the priority of a process
uint64 sys_getpriority(void){

  int pid;
  //puntero al pcb de un proceso
  struct proc *p;

  //leemos el argumento mandado utilizando la funcion de syscall.c para leer entero
  //y leemos el primer argumento
  argint(0, &pid);

  
  //recorremos el array de procesos y buscamos el proceso con PID y que este activo

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED && p->pid == pid){
      return p->priority;
    }
  }

  return -21; //devuelvo un valor de prioridad imposible, no puedo devolver -1 ya que es un valor valido
  
}