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
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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