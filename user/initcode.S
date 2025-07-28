.globl start
.section .text
start:
  la a0, init       # Dirección de "/init"
  la a1, argv       # Dirección de argv[]
  li a7, 7          # syscall exec
  ecall

  li a7, 2          # syscall exit
  ecall

.section .data
.align 12          # <-- Esto es CLAVE: aleja `.data` lo suficiente
init:
  .string "/init"

.align 3
argv:
  .quad init
  .quad 0
