#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "vf2_gpio.h"

//DEBUG main
#ifndef DBG_MAIN
#define DBG_MAIN 0
#endif

#define MAIN_DEBUG(...)              \
  do {                             \
    if(DBG_MAIN)                     \
      printf(__VA_ARGS__);         \
  } while(0)



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
  //uartputc_sync('X' + hartid % 26);  // imprime letras distintas por hart  // <-- Si ves 'X' en minicom, UART funciona correctamente
  //sbi_console = 1;  //Activar salida por consola OpenSBI (UART por defecto no iniciado)
  MAIN_DEBUG("xv6-UC: starting on hart %ld...\r\n", hartid);

  if(boothartid == -1){

    boothartid = hartid;

    consoleinit();
    printfinit();
    MAIN_DEBUG("\n");
    MAIN_DEBUG("xv6-UC version kernel is booting\r\n");
    MAIN_DEBUG("\n");


    kinit();         // physical page allocator
    
    MAIN_DEBUG("kinit done\r\n");
    kvminit();       // create kernel page table
    MAIN_DEBUG("kvminit done\r\n");
    MAIN_DEBUG("kernel_pagetable at %p\r\n", kernel_pagetable);
    kvminithart();   // turn on paging
    MAIN_DEBUG("kvminithart done\r\n");
    vf2_gpio_init();
    MAIN_DEBUG("gpio leds init done\r\n");
    /*pte_t *usb_pte;

    usb_pte = walk(kernel_pagetable,
                  VF2_STG_SYSCON_BASE,
                  0);

    if(usb_pte == 0)
      panic("no USB STG PTE");

    MAIN_DEBUG("USB PTE before=%p\n", (void *)*usb_pte);

    *usb_pte |= PTE_A | PTE_D;

    sfence_vma();
    __sync_synchronize();

    MAIN_DEBUG("USB PTE after=%p\n", (void *)*usb_pte);
    MAIN_DEBUG("  valid=%d read=%d write=%d accessed=%d dirty=%d\n",
          ((*usb_pte & PTE_V) != 0),
          ((*usb_pte & PTE_R) != 0),
          ((*usb_pte & PTE_W) != 0),
          ((*usb_pte & PTE_A) != 0),
          ((*usb_pte & PTE_D) != 0));

    MAIN_DEBUG("  physical=%p\n",
          (void *)PTE2PA(*usb_pte));*/
    procinit();      // process table
    MAIN_DEBUG("procinit done\r\n");
    trapinit();      // trap vectors
    MAIN_DEBUG("trapinit done\r\n");
    trapinithart();  // install kernel trap vector
    MAIN_DEBUG("trapinit hart done\r\n");
    //Ahora los fallos MMIO producirán un diagnóstico en lugar de parecer un bloqueo silencioso, porque se inicializa 
    //el hdmi antes de las interrupciones
    hdmi_init();
    MAIN_DEBUG("hdmi init done\r\n");
    //debe llamarse despues de inicializar el hdmi, porque sino no habria una salia de video funcional aun
    fbconsole_init();
    //esta sera la primera linea que debe aparecer en el hdmi 
    MAIN_DEBUG("hdmi framebuffer console ready\r\n");
    plicinit();      // set up interrupt controller
    MAIN_DEBUG("plicinit done\r\n");
    plicinithart();  // ask PLIC for device interrupts
    MAIN_DEBUG("plicinithart done\r\n");

    if(vf2_usb_keyboard_init() < 0)
    MAIN_DEBUG("vf2 USB keyboard initialization failed\n");
    else
    MAIN_DEBUG("vf2 USB keyboard initialization done\n");

    binit();         // buffer cache
    MAIN_DEBUG("binit done\r\n");
    iinit();         // inode table
    MAIN_DEBUG("iinit done\r\n");
    fileinit();      // file table
    MAIN_DEBUG("fileinit done\r\n");
    sd_init();       // microSD disk
    MAIN_DEBUG("sd_init done\r\n");
    //virtio_disk_init();  emulated hard disk
    //MAIN_DEBUG("virtio_disk_init done\r\n");
    userinit();      // first user process
    MAIN_DEBUG("userinit done\r\n");
    __sync_synchronize();
    MAIN_DEBUG("sync_synchronize done\r\n");


    //mensajes boot solo frambuffer
    printf("\n");
    printf("xv6-UC version kernel is booting\r\n");
    printf("\n");
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    MAIN_DEBUG("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler(); //este es el planificador y nunca termina, definicion en proc.c    
}
