#Asignar constantes a un nombre con la directiva .equ
.equ SYS_exit, 2
.equ SYS_write, 16
.equ MSG_LEN, 18

.text
.globl _start
_start:
  #cargar un inmediato para los argumentos
  li a0, 1
  la a1, message
  li a2, MSG_LEN
  li a7, SYS_write
  ecall
  #al regresar, hacer un exit(0)
  li a0, 0
  li a7, SYS_exit
  ecall

.rodata
message:
  .ascii "Hola desde xv6!\n"
