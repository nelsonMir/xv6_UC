.section .text
.globl _entry
_entry:
    # a0 = hartid, a1 = dtb_pa
    li t0, 4096 * 4       # tamaño de pila por hart = 16 KiB
    mv t1, a0             # t1 = hartid
    mul t0, t0, t1        # offset = hartid * tamaño
    la sp, boot_stack_top
    sub sp, sp, t0        # sp = boot_stack_top - offset

    call main

loop:
    j loop

.section .bss.stack
.align 12
.globl boot_stack
boot_stack:
    .space 4096 * 4 * 5     # espacio para 5 harts (5 * 16 KiB = 80 KiB)
.globl boot_stack_top
boot_stack_top:
