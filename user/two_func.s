.equ SYS_write, 16
.equ MSG_LEN, 12
.text
.globl print_ok
print_ok:
  li a0, 1
  la a1, message
  li a2, MSG_LEN
  li a7, SYS_write
  ecall
  ret
.rodata
message:
  .ascii "multiobj ok\n"
