// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
// kernel/memlayout.h
#define UART0_IRQ 32
  



// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart)   (PLIC + 0x2100   + ((hart)-1)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x202000 + ((hart)-1)*0x2000)
#define PLIC_SCLAIM(hart)    (PLIC + 0x202004 + ((hart)-1)*0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define FRAMEBUFFER_SIZE  (8*1024*1024L)
#define FRAMEBUFFER_PA    0x87800000L

/*
 * El allocator solo puede utilizar memoria inferior al framebuffer.
 */
#define PHYSTOP FRAMEBUFFER_PA

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
// Tamaño de pila de kernel y guardia
#define KSTACK_SIZE   (2*PGSIZE)     // 8 KiB de pila
#define KSTACK_GUARD  (1*PGSIZE)     // 4 KiB sin mapear
// Cada pila de kernel se coloca *debajo* del TRAPFRAME, con una guard por encima.
// OJO: la guard page es la de *arriba*; NO se mapea.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)*(KSTACK_SIZE + KSTACK_GUARD))

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)


// VisionFive 2 / JH7110: microSD controller
#define MMC1       0x16020000L
#define MMC1_SIZE  0x10000L


// VisionFive 2 / JH7110 display subsystem

// SiFive/JH7110 composable L2 cache controller
#define CCACHE_BASE      0x02010000L
#define CCACHE_SIZE      0x00004000L

#define DC8200_BASE       0x29400000L
#define DC8200_SIZE       0x00010000L

#define HDMI_TX_BASE      0x29590000L
#define HDMI_TX_SIZE      0x00010000L

#define VOUT_CRG_BASE     0x295c0000L
#define VOUT_CRG_SIZE     0x00010000L

#define SYS_CRG_BASE      0x13020000L
#define SYS_CRG_SIZE      0x00010000L

#define JH7110_PMU_BASE   0x17030000L
#define JH7110_PMU_SIZE   0x00010000L
