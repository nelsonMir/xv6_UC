.section .text
.globl _entry
_entry:
    # a0 = hartid, a1 = dtb_pa (OpenSBI los pasa así)

    # Calcular stack por hart
    li t0, 4096 * 4        # 16 KB por hart
    mv t1, a0              # hartid
    mul t0, t0, t1         # offset = hartid * 16 KB
    la t2, boot_stack_top
    sub sp, t2, t0         # sp = boot_stack_top - offset

    call main

hang:
    j hang

.section .bss.stack, "aw", @nobits
.align 12
.globl boot_stack
boot_stack:
    .space 4096 * 4 * 8       # espacio para 8 harts (ajustable)

.globl boot_stack_top
boot_stack_top:
