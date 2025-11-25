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
  int n;
  struct proc *p = myproc();

  argint(0, &n);

  uint64 addr = p->sz;  // valor anterior del break (lo que devuelve sbrk)

  if(n < 0){
    // Encogemos el heap: aquí sí usamos growproc, que acaba llamando a uvmdealloc().
    if(growproc(n) < 0)
      return -1;
  } else if(n > 0){
    // Lazy allocation: NO llamamos a growproc para reservar páginas.
    // Solo movemos el "límite lógico" del heap.
    uint64 newsz = p->sz + n;

    // (Opcional) podrías añadir alguna comprobación de overflow / límite:
    // if(newsz >= MAXVA) return -1;

    p->sz = newsz;
  }

  // n == 0 -> no cambia nada, solo devolvemos el break actual (como siempre).
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

//changes the priority of a process
uint64 sys_nice(void){

  int pid;
  //puntero al pcb de un proceso
  struct proc *p;
  //delta (cambio prioridad)
  int delta;
  

  //leemos el argumento mandado utilizando la funcion de syscall.c para leer entero
  //y leemos el primer argumento
  argint(0, &pid);

  //leo el delta, para el cambio de prioridad
  argint(1,&delta);


  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED && p->pid == pid){
      p->priority = p->priority + delta;
      if(p->priority < -20) p->priority = -20;
      if(p->priority > 19) p->priority = 19;
      return(0);
    }
  }

  //si no se encuentra el proceso
  return(-1);
}

//helper de sys_setscheduler
static const char*
policy_name(int p)
{
  switch(p){
  case 0: return "RR";
  case 1: return "FCFS";
  case 2: return "PRIORITIES";
  default: return "UNKNOWN";
  }
}

//cambiar el tipo de planificador 
uint64 sys_setscheduler(void)
{
  int policy;

  // Leer el argumento entero desde usuario
  argint(0, &policy);
  

  // Validar el rango (ajusta si cambias los tipos)
  if (policy < 0 || policy > 2)
    return -1;

  //guardamos el anterior planificador 
  int old = scheduler_policy;
  // Cambio "en caliente"
  scheduler_policy = policy;

  printf("setscheduler: %s -> %s\n",
         policy_name(old), policy_name(scheduler_policy));

  return 0;
}


