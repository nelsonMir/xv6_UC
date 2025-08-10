.globl start
.section .text
start:
  la a0, init       # Dirección de "/init"
  la a1, argv       # Dirección de argv[]
  li a7, 7          # syscall exec
  ecall

  li a0, 0          # código de salida por si exec falla
  li a7, 2          # syscall exit
  ecall

# Puedes usar .rodata o .data; lo importante es NO alinear a 4096
.section .rodata
.align 3            # 2^3 = 8 bytes (suficiente para .quad)
init:
  .string "/init"

.align 3
argv:
  .quad init
  .quad 0

