# berry_main.s
#
# Programa principal de la cadena de luces de la BerryClip.
# Ejecuta diez veces la secuencia completa de los seis LED.

.equ SYS_exit, 2
.equ REPEAT_COUNT, 10

.text
.globl _start
.globl berry_init
.globl berry_cycle
.globl berry_all_off

_start:
  call berry_init
  li s0, REPEAT_COUNT

berry_repeat:
  call berry_cycle
  addi s0, s0, -1
  bne s0, x0, berry_repeat

  call berry_all_off

  li a0, 0
  li a7, SYS_exit
  ecall