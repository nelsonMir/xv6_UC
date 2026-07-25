.equ SYS_exit, 2
.equ SYS_write, 16
.equ MSG_LEN, 18

.text
.globl _start
_start:
  li a0, 1
  la a1, message
  li a2, MSG_LEN
  li a7, SYS_write
  ecall
  li a0, 0
  li a7, SYS_exit
  ecall

.rodata
message:
  .ascii "Hola desde asxv6!\n"
