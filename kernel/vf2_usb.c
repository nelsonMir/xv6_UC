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

//Offsets del controlador DRD Cadence versión 0
#define CDNS3_V0_OTGCMD              0x00
#define CDNS3_V0_OTGSTS              0x04
#define CDNS3_V0_OTGSTATE            0x08
#define CDNS3_V0_OTGIVECT            0x14

//Offsets del controlador DRD Cadence versión 1
#define CDNS3_V1_DID                 0x00
#define CDNS3_V1_RID                 0x04
#define CDNS3_V1_OTGCMD              0x10
#define CDNS3_V1_OTGSTS              0x14
#define CDNS3_V1_OTGSTATE            0x18
#define CDNS3_V1_OTGIVECT            0x24

//Este registro tiene el mismo offset en las dos versiones
#define CDNS3_OTG_SIMULATE           0x40

//Bits del registro OTGCMD
#define CDNS3_OTGCMD_HOST_BUS_REQ    (1U << 1)
#define CDNS3_OTGCMD_OTG_DIS         (1U << 3)

//Bits del registro OTGSTS
#define CDNS3_OTGSTS_HOST_ACTIVE     (1U << 4)
#define CDNS3_OTGSTS_OTG_NRDY        (1U << 11)
#define CDNS3_OTGSTS_STRAP_SHIFT     12
#define CDNS3_OTGSTS_STRAP_MASK      (7U << CDNS3_OTGSTS_STRAP_SHIFT)
#define CDNS3_OTGSTS_XHCI_READY      (1U << 26)

//Valores del campo STRAP
#define CDNS3_STRAP_NO_DEFAULT       0
#define CDNS3_STRAP_HOST_OTG         1
#define CDNS3_STRAP_HOST             2
#define CDNS3_STRAP_DEVICE           4

//Número máximo de lecturas durante la activación del modo host
#define CDNS3_HOST_POLL_LIMIT        5000000U

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

/*
Espera hasta que el controlador Cadence indique que el bloque xHCI
está preparado para funcionar como host.

La función devuelve 0 cuando XHCI_READY se activa.
Devuelve -1 si se supera el límite de espera.
*/
static int
vf2_usb_wait_xhci_ready(uint64 status_address)
{
  uint32 status;
  uint32 i;

  status = 0;

  for(i = 0; i < CDNS3_HOST_POLL_LIMIT; i++){
    status = mmio_read32(status_address);

    if(status & CDNS3_OTGSTS_XHCI_READY)
      return 0;

    __asm__ volatile("nop");
  }

  printf("usb: timeout waiting for XHCI_READY\n");
  printf("usb: OTGSTS=0x%x\n", status);

  return -1;
}


/*
Muestra el contenido del registro de estado del controlador DRD.

Este registro permite comprobar el modo indicado por los straps,
si el bloque host está activo y si el xHCI está preparado.
*/
static void
vf2_usb_dump_drd_status(uint32 status)
{
  uint32 strap;

  strap = (status & CDNS3_OTGSTS_STRAP_MASK)
        >> CDNS3_OTGSTS_STRAP_SHIFT;

  printf("usb: OTGSTS=0x%x\n", status);
  printf("usb: strap=%d host_active=%d xhci_ready=%d\n",
         strap,
         (status & CDNS3_OTGSTS_HOST_ACTIVE) != 0,
         (status & CDNS3_OTGSTS_XHCI_READY) != 0);

  printf("usb: otg_not_ready=%d\n",
         (status & CDNS3_OTGSTS_OTG_NRDY) != 0);
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

int
vf2_usb_start_host(void)
{
  uint32 first_register;
  uint32 status;
  uint32 command;
  uint32 version;
  uint32 revision;
  uint64 command_address;
  uint64 status_address;
  uint64 state_address;
  uint64 interrupt_vector_address;

  printf("usb: initializing Cadence DRD host role\n");

  /*
  El primer registro permite distinguir las dos versiones del
  controlador DRD Cadence.

  En la versión 0 el primer registro es OTGCMD y su lectura inicial
  devuelve cero.

  En la versión 1 el primer registro contiene el identificador DID
  y normalmente devuelve un valor distinto de cero.
  */
  first_register =
    mmio_read32(VF2_USB_OTG_BASE + CDNS3_V1_DID);

  if(first_register == 0){
    printf("usb: Cadence DRD version 0 detected\n");

    command_address =
      VF2_USB_OTG_BASE + CDNS3_V0_OTGCMD;

    status_address =
      VF2_USB_OTG_BASE + CDNS3_V0_OTGSTS;

    state_address =
      VF2_USB_OTG_BASE + CDNS3_V0_OTGSTATE;

    interrupt_vector_address =
      VF2_USB_OTG_BASE + CDNS3_V0_OTGIVECT;
  } else {
    version =
      mmio_read32(VF2_USB_OTG_BASE + CDNS3_V1_DID);

    revision =
      mmio_read32(VF2_USB_OTG_BASE + CDNS3_V1_RID);

    printf("usb: Cadence DRD version 1 detected\n");
    printf("usb: DID=0x%x RID=0x%x\n",
           version,
           revision);

    command_address =
      VF2_USB_OTG_BASE + CDNS3_V1_OTGCMD;

    status_address =
      VF2_USB_OTG_BASE + CDNS3_V1_OTGSTS;

    state_address =
      VF2_USB_OTG_BASE + CDNS3_V1_OTGSTATE;

    interrupt_vector_address =
      VF2_USB_OTG_BASE + CDNS3_V1_OTGIVECT;
  }

  /*
  El driver oficial escribe uno en SIMULATE durante la detección
  e inicialización de la interfaz DRD.
  */
  mmio_write32(VF2_USB_OTG_BASE + CDNS3_OTG_SIMULATE,
               1);

  //Se eliminan las interrupciones pendientes del bloque DRD
  mmio_write32(interrupt_vector_address,
               0xffffffffU);

  status = mmio_read32(status_address);

  printf("usb: DRD status before host request\n");
  vf2_usb_dump_drd_status(status);

  if(status & CDNS3_OTGSTS_OTG_NRDY){
    printf("usb: Cadence OTG controller is not ready\n");
    return -1;
  }

  /*
  Se solicita el bus para el motor host y se desactiva el
  funcionamiento OTG dinámico.

  Esta es la operación que faltaba antes de reiniciar el xHCI.
  */
  command = CDNS3_OTGCMD_HOST_BUS_REQ |
            CDNS3_OTGCMD_OTG_DIS;

  printf("usb: requesting Cadence host bus\n");
  printf("usb: writing OTGCMD=0x%x at %p\n",
         command,
         (void *)command_address);

  mmio_write32(command_address,
               command);

  /*
  El xHCI no debe reiniciarse hasta que el wrapper Cadence
  active el bit XHCI_READY.
  */
  if(vf2_usb_wait_xhci_ready(status_address) < 0){
    printf("usb: Cadence host role did not start\n");
    printf("usb: OTGSTATE=0x%x\n",
           mmio_read32(state_address));
    return -1;
  }

  status = mmio_read32(status_address);

  printf("usb: DRD status after host request\n");
  vf2_usb_dump_drd_status(status);

  printf("usb: OTGSTATE=0x%x\n",
         mmio_read32(state_address));

  printf("usb: Cadence host role ready\n");

  return 0;
}