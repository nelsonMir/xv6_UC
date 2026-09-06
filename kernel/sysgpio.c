/*
sysgpio.c

Llamadas al sistema que permiten a un programa de usuario configurar y
escribir únicamente los GPIO autorizados por el driver de la VisionFive 2.
*/

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "vf2_gpio.h"

uint64
sys_gpio_output(void)
{
  int gpio;

  argint(0, &gpio);
  return vf2_gpio_output(gpio);
}

uint64
sys_gpio_write(void)
{
  int gpio;
  int value;

  argint(0, &gpio);
  argint(1, &value);
  return vf2_gpio_write(gpio, value);
}