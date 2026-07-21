#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "vf2_usb.h"

#define BIT(n) (1U << (n))

/*
  Los registros de reloj del JH7110 están separados por cuatro bytes
  El bit 31 habilita los relojes que disponen de gate
 */
#define JH7110_CLK_ENABLE       BIT(31)

// iindices de reloj del STGCRG
#define STGCLK_USB0_APB         1
#define STGCLK_USB0_UTMI_APB    2
#define STGCLK_USB0_AXI         3
#define STGCLK_USB0_LPM         4
#define STGCLK_USB0_STB         5
#define STGCLK_USB0_APP_125     6

//Registros de reset del STGCRG
#define STG_RESET_ASSERT        0x74
#define STG_RESET_STATUS        0x78

#define STGRST_USB0_AXI         BIT(7)
#define STGRST_USB0_APB         BIT(8)
#define STGRST_USB0_UTMI_APB    BIT(9)
#define STGRST_USB0_PWRUP       BIT(10)

#define STGRST_USB_MASK \
  (STGRST_USB0_AXI | STGRST_USB0_APB | \
   STGRST_USB0_UTMI_APB | STGRST_USB0_PWRUP)

//STG syscon + 0x4
#define STG_USB_MODE_OFFSET     0x04

#define USB_STRAP_HOST          BIT(17)
#define USB_STRAP_MASK          (BIT(16) | BIT(17) | BIT(18))
#define USB_SUSPENDM_HOST       BIT(19)
#define USB_SUSPENDM_MASK       BIT(19)

#define USB_SUSPENDM_BYPS       BIT(20)
#define USB_PLL_EN              BIT(22)
#define USB_REFCLK_MODE         BIT(23)
#define USB_MISC_CFG_MASK       \
  (BIT(20) | BIT(21) | BIT(22) | BIT(23))

// PHY USB2 en 0x10200000
#define USB_PHY_CLK_MODE        0x00
#define USB_PHY_KEEPALIVE       0x04

#define USB_PHY_RX_NORMAL_PWR   BIT(1)
#define USB_PHY_LS_KEEPALIVE    BIT(4)

// SYS syscon + 0x18
#define SYS_USB_SPLIT_OFFSET    0x18
#define USB_PDRSTN_SPLIT        BIT(17)

static inline uint32
mmio_read32(uint64 address)
{
  return *(volatile uint32 *)address;
}

static inline void
mmio_write32(uint64 address, uint32 value)
{
  *(volatile uint32 *)address = value;
  __sync_synchronize();
}

static void
mmio_set_bits(uint64 address, uint32 bits)
{
  uint32 value = mmio_read32(address);
  mmio_write32(address, value | bits);
}

static void
mmio_clear_bits(uint64 address, uint32 bits)
{
  uint32 value = mmio_read32(address);
  mmio_write32(address, value & ~bits);
}

static void
mmio_update_bits(uint64 address, uint32 mask, uint32 value)
{
  uint32 old = mmio_read32(address);

  old &= ~mask;
  old |= value & mask;

  mmio_write32(address, old);
}

static uint64
stg_clock_register(uint32 index)
{
  return VF2_STG_CRG_BASE + 4U * index;
}

static void
vf2_usb_enable_gate(uint32 index)
{
  mmio_set_bits(stg_clock_register(index), JH7110_CLK_ENABLE);
}

/*
  LPM y STB tienen gate y divisor
 
  Se conserva el divisor que dejó el firmware al arrancar. Si vale cero,
  se establece divisor 1 para evitar que el reloj quede parado
 */
static void
vf2_usb_enable_divided_clock(uint32 index)
{
  uint64 reg = stg_clock_register(index);
  uint32 value = mmio_read32(reg);

  if((value & 0x00ffffffU) == 0)
    value |= 1;

  value |= JH7110_CLK_ENABLE;
  mmio_write32(reg, value);
}

static int
vf2_usb_wait_reset_deasserted(void)
{
  uint32 i;

  for(i = 0; i < 1000000; i++){
    uint32 status =
      mmio_read32(VF2_STG_CRG_BASE + STG_RESET_STATUS);

    
    //En el JH7110, para estas líneas un bit de estado a 1
    //representa reset liberado
    if((status & STGRST_USB_MASK) == STGRST_USB_MASK)
      return 0;
  }

  return -1;
}

void
vf2_usb_init(void)
{
  uint64 mode_reg;
  uint32 before;
  uint32 after;
  uint32 reset_assert;
  uint32 reset_status;

  printf("usb: initializing JH7110 wrapper\n");

  /*
    1. Configurar PLL, referencia y modo host antes de liberar
    los resets, siguiendo el glue driver de Linux
   */
  mode_reg = VF2_STG_SYSCON_BASE + STG_USB_MODE_OFFSET;

  before = mmio_read32(mode_reg);
  printf("usb: stg mode before=0x%x\n", before);

  mmio_update_bits(mode_reg,
                   USB_MISC_CFG_MASK,
                   USB_SUSPENDM_BYPS |
                   USB_PLL_EN |
                   USB_REFCLK_MODE);

  mmio_update_bits(mode_reg,
                   USB_STRAP_MASK,
                   USB_STRAP_HOST);

  mmio_update_bits(mode_reg,
                   USB_SUSPENDM_MASK,
                   USB_SUSPENDM_HOST);

  after = mmio_read32(mode_reg);
  printf("usb: stg mode after=0x%x\n", after);

  //2. Activar los relojes del wrapper
  vf2_usb_enable_gate(STGCLK_USB0_APB);
  vf2_usb_enable_gate(STGCLK_USB0_UTMI_APB);
  vf2_usb_enable_gate(STGCLK_USB0_AXI);
  vf2_usb_enable_divided_clock(STGCLK_USB0_LPM);
  vf2_usb_enable_divided_clock(STGCLK_USB0_STB);
  vf2_usb_enable_gate(STGCLK_USB0_APP_125);

  printf("usb: clocks enabled\n");

  /*
    3. Liberar los cuatro resets USB
   
    El controlador de reset de Linux realiza un read-modify-write
    y limpia el bit para liberar cada reset
   */
  reset_assert =
  mmio_read32(VF2_STG_CRG_BASE + STG_RESET_ASSERT);

  printf("usb: reset assert before=0x%x\n", reset_assert);

  mmio_clear_bits(VF2_STG_CRG_BASE + STG_RESET_ASSERT,
                    STGRST_USB_MASK);

  if(vf2_usb_wait_reset_deasserted() < 0){
    reset_status =
      mmio_read32(VF2_STG_CRG_BASE + STG_RESET_STATUS);

    printf("usb: reset timeout, status=0x%x\n",
           reset_status);
    return;
  }

  reset_status =
    mmio_read32(VF2_STG_CRG_BASE + STG_RESET_STATUS);

  printf("usb: resets deasserted, status=0x%x\n",
         reset_status);

  //4. Inicializar el PHY USB2
  mmio_set_bits(VF2_USB_PHY_BASE + USB_PHY_CLK_MODE,
                USB_PHY_RX_NORMAL_PWR);

  /*
   En modo host se habilita LS keep-alive para dispositivos
   low-speed, como algunos teclados
   */
  mmio_set_bits(VF2_USB_PHY_BASE + USB_PHY_KEEPALIVE,
                USB_PHY_LS_KEEPALIVE);

  //Conectar el PHY USB2 al controlador Cadence
  mmio_set_bits(VF2_SYS_SYSCON_BASE + SYS_USB_SPLIT_OFFSET,
                USB_PDRSTN_SPLIT);

  printf("usb: PHY configured\n");

  //Pequeña espera para que PLL, PHY y resets se estabilicen
  for(volatile uint32 i = 0; i < 100000; i++)
    __asm__ volatile("nop");

  printf("usb: JH7110 wrapper ready\n");
}