.equ SYS_exit, 2
.text
.globl _start
.globl print_ok
_start:
  call print_ok
  li a0, 0
  li a7, SYS_exit
  ecall
