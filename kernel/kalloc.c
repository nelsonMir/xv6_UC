// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// DEBUG: salida UART síncrona para trazar sin printf()
extern void uartputc_sync(int c);
static void puts_sync(const char *s){
  while(*s) uartputc_sync(*s++);
}
static void puthex64(uint64 x){
  for(int i=60;i>=0;i-=4){
    int d = (x>>i)&0xF;
    uartputc_sync(d<10 ? '0'+d : 'a'+(d-10));
  }
}

// DEBUG: si quieres limitar el free a un trocito para probar el boot, pon 1
#ifndef DEBUG_LIMIT_FREE
#define DEBUG_LIMIT_FREE 0
#endif

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  // DEBUG: hitos de arranque del allocator
  puts_sync("K1"); // llegó a kinit

  initlock(&kmem.lock, "kmem");
  puts_sync("K2"); // pasó initlock

  // DEBUG: imprime límites que usaremos
  puts_sync(" end="); puthex64((uint64)end);
  puts_sync(" top="); puthex64((uint64)PHYSTOP);
  uartputc_sync('\r'); uartputc_sync('\n');

#if DEBUG_LIMIT_FREE
  // DEBUG: limitar el free a 1 MiB tras 'end' para ver si arranca
  uint64 safe_start = (uint64)PGROUNDUP((uint64)end);
  uint64 safe_top   = safe_start + 1*1024*1024; // 1 MiB
  if(safe_top > (uint64)PHYSTOP) safe_top = (uint64)PHYSTOP;
  freerange((void*)safe_start, (void*)safe_top);
#else
  freerange(end, (void*)PHYSTOP);
#endif

  puts_sync("K3"); // terminó freerange()
  uartputc_sync('\r'); uartputc_sync('\n');
}

void
freerange(void *pa_start, void *pa_end)
{
  // DEBUG: traza de rango y progreso
  puts_sync("F1 ");
  puthex64((uint64)pa_start);
  puts_sync("..");
  puthex64((uint64)pa_end);
  uartputc_sync('\r'); uartputc_sync('\n');

  char *p = (char*)PGROUNDUP((uint64)pa_start);

  uint64 cnt = 0; // DEBUG: contador de páginas liberadas
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree(p);
    if((++cnt & 0xFF) == 0){ // cada 256 páginas
      puts_sync("F+ ");
      puthex64((uint64)p);
      uartputc_sync('\r'); uartputc_sync('\n');
    }
  }

  puts_sync("F2 "); // DEBUG: fin de bucle y última dirección tocada
  puthex64((uint64)p);
  uartputc_sync('\r'); uartputc_sync('\n');
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

//retorna el numero de paginas libres y la usaremos en la syscall sys_freemem
uint64 free_mem(void){

  struct run *r; //cada nodo de la lista de paginas libres en "kmem" es un puntero a una estructura 
  //"run" que es la pagina en si 

  //contador del numero de pags libres, debe ser uint64 porque con reg de 64 bits trabaja riscv
  uint64 count = 0;

  //accedemos a la var global "kmem" que apunta al inicio de paginas libres y usamos el lock para 
  //evitar que otro proceso o CPU quiera acceder a esta para modificarla
  acquire(&kmem.lock);

  //obtenemos el primer puntero a la primera pagina libre
  r = kmem.freelist;

  //vamos a recorrer toda la lista de paginas libres
  while(r){
    count++;
    r = r->next;
  }

  release(&kmem.lock);

  return count * PGSIZE;
}

//retorna el tamano de una pagina 
uint64 get_page_size(void){

  return PGSIZE;
}
