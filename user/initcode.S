.globl start
.section .text
start:
  la a0, init       # a0 ← dirección de "/init"
  la a1, argv       # a1 ← dirección de argv[]
  li a7, 7          # syscall exec
  ecall

  li a7, 2          # syscall exit
  ecall

.section .data
.align 3
init:
  .string "/init"

.align 3
argv:
  .quad init
  .quad 0
