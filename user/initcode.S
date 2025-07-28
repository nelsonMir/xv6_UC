.globl start
.section .text
.align 2
start:
        # a0 = path, a1 = argv
        la a0, init
        la a1, argv
        li a7, 7   # syscall exec tiene número 7

        ecall

exit:
        li a7, 1   # syscall exit tiene número 1

        ecall
        j exit       # infinite loop in case exit fails

.section .rodata
.align 2
init:
        .string "/init"

.align 3
argv:
        .quad init
        .quad 0
