# berry_leds.s
#
# Funciones auxiliares para controlar los seis LED de la placa de Tecnofilo
# similar a BerryClip conectada a la fila de pines impares 1-25 de la
# VisionFive 2.
#
# RED1 -> pin fisico 21 -> GPIO53
# RED2 -> pin fisico 19 -> GPIO52
# YEL1 -> pin fisico 23 -> GPIO48
# YEL2 -> pin fisico 11 -> GPIO42
# GRE1 -> pin fisico 15 -> GPIO47
# GRE2 -> pin fisico 13 -> GPIO43

.equ SYS_sleep, 13
.equ SYS_gpio_output, 31
.equ SYS_gpio_write, 32

# Tiempo que permanece encendido cada LED.
.equ LED_DELAY_TICKS, 10

.equ LED_RED1_GPIO, 53
.equ LED_RED2_GPIO, 52
.equ LED_YEL1_GPIO, 48
.equ LED_YEL2_GPIO, 42
.equ LED_GRE1_GPIO, 47
.equ LED_GRE2_GPIO, 43

.text
.globl berry_init
.globl berry_cycle
.globl berry_all_off

# Configura los seis GPIO como salidas.
berry_init:
  addi sp, sp, -16
  sd ra, 8(sp)

  li a0, LED_RED1_GPIO
  call gpio_output_call

  li a0, LED_RED2_GPIO
  call gpio_output_call

  li a0, LED_YEL1_GPIO
  call gpio_output_call

  li a0, LED_YEL2_GPIO
  call gpio_output_call

  li a0, LED_GRE1_GPIO
  call gpio_output_call

  li a0, LED_GRE2_GPIO
  call gpio_output_call

  ld ra, 8(sp)
  addi sp, sp, 16
  ret

# Ejecuta una vuelta completa de la cadena de luces.
berry_cycle:
  addi sp, sp, -16
  sd ra, 8(sp)

  li a0, LED_RED1_GPIO
  call led_step

  li a0, LED_RED2_GPIO
  call led_step

  li a0, LED_YEL1_GPIO
  call led_step

  li a0, LED_YEL2_GPIO
  call led_step

  li a0, LED_GRE1_GPIO
  call led_step

  li a0, LED_GRE2_GPIO
  call led_step

  ld ra, 8(sp)
  addi sp, sp, 16
  ret

# Apaga explicitamente todos los LED.
berry_all_off:
  addi sp, sp, -16
  sd ra, 8(sp)

  li a0, LED_RED1_GPIO
  li a1, 0
  call gpio_write_call

  li a0, LED_RED2_GPIO
  li a1, 0
  call gpio_write_call

  li a0, LED_YEL1_GPIO
  li a1, 0
  call gpio_write_call

  li a0, LED_YEL2_GPIO
  li a1, 0
  call gpio_write_call

  li a0, LED_GRE1_GPIO
  li a1, 0
  call gpio_write_call

  li a0, LED_GRE2_GPIO
  li a1, 0
  call gpio_write_call

  ld ra, 8(sp)
  addi sp, sp, 16
  ret

# Enciende un LED, espera y despues lo apaga.
# Entrada: a0 = numero GPIO del LED.
led_step:
  addi sp, sp, -16
  sd ra, 8(sp)
  sd s0, 0(sp)

  mv s0, a0

  li a1, 1
  call gpio_write_call

  li a0, LED_DELAY_TICKS
  call delay_ticks

  mv a0, s0
  li a1, 0
  call gpio_write_call

  ld s0, 0(sp)
  ld ra, 8(sp)
  addi sp, sp, 16
  ret

# Configura como salida el GPIO recibido en a0.
gpio_output_call:
  li a7, SYS_gpio_output
  ecall
  ret

# Escribe en el GPIO: a0 = GPIO, a1 = 0/1.
gpio_write_call:
  li a7, SYS_gpio_write
  ecall
  ret

# Espera el numero de ticks recibido en a0.
delay_ticks:
  li a7, SYS_sleep
  ecall
  ret