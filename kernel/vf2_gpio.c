/*
vf2_gpio.c

Control mínimo de los GPIO del bloque SYS IOMUX del JH7110.
Solo permite utilizar los seis GPIO conectados a los LED de la BerryClip.
Los accesos se realizan desde el kernel para no exponer todo el espacio MMIO
a los programas de usuario.
*/

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "spinlock.h"
#include "defs.h"
#include "vf2_gpio.h"

#define JH7110_SYS_DOEN       0x000L
#define JH7110_SYS_DOUT       0x040L
#define JH7110_SYS_PADCFG     0x120L

#define JH7110_DOEN_MASK      0x3fU
#define JH7110_DOUT_MASK      0x7fU

#define JH7110_GPOEN_ENABLE   0U
#define JH7110_GPOEN_DISABLE  1U
#define JH7110_GPOUT_LOW      0U
#define JH7110_GPOUT_HIGH     1U

#define JH7110_PADCFG_IE      (1U << 0)
#define JH7110_PADCFG_DS_MASK (3U << 1)
#define JH7110_PADCFG_DS_4MA  (1U << 1)
#define JH7110_PADCFG_PU      (1U << 3)
#define JH7110_PADCFG_PD      (1U << 4)
#define JH7110_PADCFG_SMT     (1U << 6)

static struct spinlock vf2_gpio_lock;

static uint32
vf2_gpio_read32(uint64 address)
{
  return *(volatile uint32 *)address;
}

static void
vf2_gpio_write32(uint64 address, uint32 value)
{
  *(volatile uint32 *)address = value;
}

static int
vf2_gpio_allowed(int gpio)
{
  return gpio == 53 || gpio == 52 || gpio == 48 ||
         gpio == 42 || gpio == 47 || gpio == 43;
}

/*
Cada registro DOEN o DOUT contiene cuatro GPIO.
Cada GPIO utiliza un campo de ocho bits dentro del registro.
*/
static void
vf2_gpio_write_lane(uint64 register_offset, int gpio,
                    uint32 field_mask, uint32 field_value)
{
  uint64 address;
  uint32 shift;
  uint32 mask;
  uint32 value;

  address = VF2_GPIO_BASE + register_offset + 4 * (gpio / 4);
  shift = 8 * (gpio % 4);
  mask = field_mask << shift;

  value = vf2_gpio_read32(address);
  value &= ~mask;
  value |= (field_value & field_mask) << shift;
  vf2_gpio_write32(address, value);
}

/*
Selecciona la función 0, que corresponde al funcionamiento GPIO normal.
Solo se incluyen los seis pines utilizados por la BerryClip.
*/
static void
vf2_gpio_select_gpio_function(int gpio)
{
  uint64 address;
  uint32 shift;
  uint32 value;
  uint32 mask;

  switch(gpio){
  case 42:
    address = VF2_GPIO_BASE + 0x2a8L;
    shift = 3;
    break;
  case 43:
    address = VF2_GPIO_BASE + 0x2a8L;
    shift = 6;
    break;
  case 47:
    address = VF2_GPIO_BASE + 0x2a8L;
    shift = 18;
    break;
  case 48:
    address = VF2_GPIO_BASE + 0x2a8L;
    shift = 21;
    break;
  case 52:
    address = VF2_GPIO_BASE + 0x2acL;
    shift = 0;
    break;
  case 53:
    address = VF2_GPIO_BASE + 0x2acL;
    shift = 2;
    break;
  default:
    return;
  }

  mask = 0x3U << shift;
  value = vf2_gpio_read32(address);
  value &= ~mask;
  vf2_gpio_write32(address, value);
}

static void
vf2_gpio_configure_pad_output(int gpio)
{
  uint64 address;
  uint32 value;

  address = VF2_GPIO_BASE + JH7110_SYS_PADCFG + 4 * gpio;
  value = vf2_gpio_read32(address);

  value &= ~(JH7110_PADCFG_IE |
             JH7110_PADCFG_DS_MASK |
             JH7110_PADCFG_PU |
             JH7110_PADCFG_PD |
             JH7110_PADCFG_SMT);
  value |= JH7110_PADCFG_DS_4MA;

  vf2_gpio_write32(address, value);
}

void
vf2_gpio_init(void)
{
  initlock(&vf2_gpio_lock, "vf2_gpio");
}

int
vf2_gpio_output(int gpio)
{
  if(!vf2_gpio_allowed(gpio))
    return -1;

  acquire(&vf2_gpio_lock);

  // Deshabilita temporalmente la salida mientras se configura el pin.
  vf2_gpio_write_lane(JH7110_SYS_DOEN, gpio,
                      JH7110_DOEN_MASK, JH7110_GPOEN_DISABLE);

  // Deja preparado el nivel bajo para que el LED comience apagado.
  vf2_gpio_write_lane(JH7110_SYS_DOUT, gpio,
                      JH7110_DOUT_MASK, JH7110_GPOUT_LOW);

  vf2_gpio_select_gpio_function(gpio);
  vf2_gpio_configure_pad_output(gpio);

  // En el JH7110 el valor cero habilita el driver de salida.
  vf2_gpio_write_lane(JH7110_SYS_DOEN, gpio,
                      JH7110_DOEN_MASK, JH7110_GPOEN_ENABLE);

  __sync_synchronize();
  release(&vf2_gpio_lock);
  return 0;
}

int
vf2_gpio_write(int gpio, int value)
{
  if(!vf2_gpio_allowed(gpio))
    return -1;

  if(value != 0 && value != 1)
    return -1;

  acquire(&vf2_gpio_lock);

  vf2_gpio_write_lane(JH7110_SYS_DOUT, gpio,
                      JH7110_DOUT_MASK,
                      value ? JH7110_GPOUT_HIGH : JH7110_GPOUT_LOW);

  __sync_synchronize();
  release(&vf2_gpio_lock);
  return 0;
}
