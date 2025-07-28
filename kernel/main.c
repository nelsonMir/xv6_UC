#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;
//extern int sbi_console;  
extern void uartputc_sync(int c);
//para poder inicializar con cualquier hart 
volatile static int boothartid = -1;
extern pagetable_t kernel_pagetable;


// start() jumps here in supervisor mode on all CPUs.
void main(unsigned long hartid, unsigned long dtb_pa)
{
   /*if (hartid != 0) {
    uartputc_sync('H');
    uartputc_sync('!');
    uartputc_sync('\n');
    for (;;)
      ;
  }*/
   // Mensaje de depuración básico, directo a UART
  uartputc_sync('X' + hartid % 26);  // imprime letras distintas por hart  // <-- Si ves 'X' en minicom, UART funciona correctamente
  //sbi_console = 1;  //Activar salida por consola OpenSBI (UART por defecto no iniciado)
  printf("xv6-UC: starting on hart %ld...\n", hartid);

  if(boothartid == -1){

    boothartid = hartid;

    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6-UC version kernel is booting\n");
    printf("\n");

    kinit();         // physical page allocator
    
    printf("kinit done\n");
    kvminit();       // create kernel page table
    printf("kvminit done\r\n");
    printf("kernel_pagetable at %p\r\n", kernel_pagetable);
    kvminithart();   // turn on paging
    printf("kvminithart done\n");
    procinit();      // process table
    printf("procinit done\r\n");
    trapinit();      // trap vectors
    printf("trapinit done\r\n");
    trapinithart();  // install kernel trap vector
    printf("trapinit hart done\r\n");
    plicinit();      // set up interrupt controller
    printf("plicinit done\r\n");
    plicinithart();  // ask PLIC for device interrupts
    printf("plicinithart done\r\n");
    binit();         // buffer cache
    printf("binit done\r\n");
    iinit();         // inode table
    printf("iinit done\r\n");
    fileinit();      // file table
    printf("fileinit done\r\n");
    virtio_disk_init(); // emulated hard disk
    printf("virtio_disk_init done\r\n");
    userinit();      // first user process
    printf("userinit done\r\n");
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler(); //este es el planificador y nunca termina, definicion en proc.c    
}
