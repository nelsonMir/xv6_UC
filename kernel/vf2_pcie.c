/*
vf2_pcie.c

Primera etapa del driver PCI Express para la VisionFive 2

Aquí realizaré únicamente el encendido y la inicialización básica
de PCIe0 del SoC JH71100 hasta conseguir un enlace activo con el chip VL805 (controlador USB).
Aquí solo se prepara el lado del PCIe0, en mis mensajes de depuración cuando vea el mensaje "pcie0: port link up"
significa que la capa física (la encargada de mandar la información) y la capa de enlace PCIe ya funcionan:
En este fichero se configurará el Root POrt, ósea el controlador principal de la conexión 
PCIe, que pertence al procesador, y este estará encargado de:
- Activar el enlace
- Generar las transacciones PCIe
- Acceder a la configuración del dispositivo (EndPoint ---> dispositivo PCIe conectado ----->
    en este caso es el chip "VL805": Es el chip físico de la vf2 que cumple la función como controlador 
    USB. Para comunicarme con este chip se tiene que utiliza el estándar xHCI, pero eso lo haré en otro 
    fichero, aquí solo inicializaré el Root Port de la conexión PCIe, en otro fichero me encargaré del otro 
    lado de la comunicación, ósea del endpoint)
- Ofrecer ventanas de memoria para acceder al periférico. 

EN concreto, aquí haré las siguientes acciones:
- Configura el Multi-PHY de 0x10210000 en modo PCIe.
- Selecciona el reloj de referencia de PCIe0.
- Activa los relojes del controlador.
- Libera los resets del dominio STG.
- Mantiene el dispositivo conectado en reset mediante GPIO26.
- Configura el Root Port PLDA.
- Libera PERST después del tiempo mínimo requerido.
- Espera hasta que el enlace PCIe esté activo.


*/

#include "types.h"
#include "param.h"
#include "memlayout.h" //he puesto aquí las direcciones físicas de los bloqueas hadrware que usa este driver
#include "riscv.h"
#include "defs.h"
#include "vf2_pcie.h"


#define BIT(n) (1U << (n)) /*con esta macro podré expresar un bit concreto, ósea le paso 
un num entero y me devuelve el bit activo, si le mando BIT(50) me devuelve el bit 50 a valor 1 y los demás a 0
EJ:
BIT(0)  = 0x00000001
BIT(1)  = 0x00000002
BIT(3)  = 0x00000008
BIT(31) = 0x80000000

LO usaré para acceder a los bits independientes de los registros MMIO*/


//El contador "time" del JH7110 funciona a cuatro ticks por microsegundo, ósea avanza 4 unidades por microsegundo ---> 4 ticks/  micro seg ----> 4 Mhz
#define JH7110_TICKS_PER_US              4ULL


/*constantes que representan bits de los registros MMIO*/

//Muchos componentes del SoC JH7110 tienen un registro para el reloj. Con el bit 31 a 1 se habilita el reloj, de lo contrario 
//el reloj estarí apagado y el bloque hw no avanzaría aunque tengamos sus registros mapeados 
#define JH7110_CLK_ENABLE                BIT(31)


/*Índices de los relojes utilizados por PCIe0*/

//estos son varios relojes del sistema 
#define SYSCLK_NOC_BUS_STG_AXI           96U //Network on CHip, es la red que comunica los bloques del SoC
////AXI es el bus interno de alto rendimiento: este reloj permite que el procesador y otros bloques accedan al dominio STG mediante el bus AXI
#define SYSCLK_IOMUX_APB                 112U  //IOMUX Selecciona qué función tiene cada pin físico

#define STGCLK_PCIE0_AXI_MST0            8U 
#define STGCLK_PCIE0_APB                 9U
#define STGCLK_PCIE0_TL                  10U
#define STGCLK_PCIE_SLV_MAIN             14U


//Registros de reset del STG CRG
#define STG_RESET_ASSERT                 0x74
#define STG_RESET_STATUS                 0x78


//Líneas de reset utilizadas por PCIe0
#define STGRST_PCIE0_AXI_MST0            BIT(11)
#define STGRST_PCIE0_AXI_SLV0            BIT(12)
#define STGRST_PCIE0_AXI_SLV             BIT(13)
#define STGRST_PCIE0_BRG                 BIT(14)
#define STGRST_PCIE0_CORE                BIT(15)
#define STGRST_PCIE0_APB                 BIT(16)

#define STGRST_PCIE0_MASK \
  (STGRST_PCIE0_AXI_MST0 | STGRST_PCIE0_AXI_SLV0 | \
   STGRST_PCIE0_AXI_SLV | STGRST_PCIE0_BRG | \
   STGRST_PCIE0_CORE | STGRST_PCIE0_APB)


//Offsets del STG syscon utilizados por PCIe0
#define STG_SYSCON_PCIE0_BASE            0x48
#define STG_SYSCON_AR_OFFSET             0x78
#define STG_SYSCON_AW_OFFSET             0x7c
#define STG_SYSCON_RP_NEP_OFFSET         0xe8
#define STG_SYSCON_LNKSTA_OFFSET         0x170

#define STG_PCIE0_AR_OFFSET \
  (STG_SYSCON_PCIE0_BASE + STG_SYSCON_AR_OFFSET)

#define STG_PCIE0_AW_OFFSET \
  (STG_SYSCON_PCIE0_BASE + STG_SYSCON_AW_OFFSET)

#define STG_PCIE0_RP_NEP_OFFSET \
  (STG_SYSCON_PCIE0_BASE + STG_SYSCON_RP_NEP_OFFSET)

#define STG_PCIE0_LNKSTA_OFFSET \
  (STG_SYSCON_PCIE0_BASE + STG_SYSCON_LNKSTA_OFFSET)


//Campos de selección de la función física
#define STG_AXI4_SLVL_AR_MASK            (0x7fffU << 8)
#define STG_AXI4_SLVL_PHY_AR(function)   ((uint32)(function) << 17)

#define STG_AXI4_SLVL_AW_MASK            0x7fffU
#define STG_AXI4_SLVL_PHY_AW(function)   ((uint32)(function) << 9)


//Configuración del reloj y del Root Port
#define STG_SYSCON_CLKREQ                BIT(22)
#define STG_SYSCON_CKREF_SRC_MASK        (3U << 18)
#define STG_SYSCON_CKREF_SRC_PCIE        (2U << 18)
#define STG_SYSCON_K_RP_NEP              BIT(8)


//Estado del enlace PCI Express
#define STG_DATA_LINK_ACTIVE             BIT(5)
#define PCIE_LINK_WAIT_RETRIES           10U
#define PCIE_LINK_WAIT_MS                100U


//Registros del Multi-PHY de PCIe0
#define PCIE_PHY_KVCO_LEVEL_OFFSET       0x28
#define PCIE_PHY_PLL_CONTROL_OFFSET      0x7c
#define PCIE_PHY_KVCO_TUNE_OFFSET        0x80

#define PCIE_PHY_KVCO_LEVEL              0x91U
#define PCIE_PHY_KVCO_TUNE               0x0cU
#define PCIE_PHY_ENABLE                  BIT(4)


//Registros del STG y SYS syscon utilizados por el Multi-PHY
#define STG_PCIE_PHY_MODE_OFFSET         0x148
#define STG_PCIE_PHY_USB_OFFSET          0x1f4
#define SYS_PCIE_PHY_CONNECT_OFFSET      0x18

#define PCIE_PHY_MODE_MASK               (3U << 20)
#define PCIE_PHY_BUS_WIDTH_MASK          (3U << 2)
#define PCIE_PHY_BUS_WIDTH_PCIE          BIT(3)
#define USB_PDRSTN_SPLIT                 BIT(17)


//Registros PLDA del controlador PCIe
#define PLDA_GEN_SETTINGS                0x80
#define PLDA_ROOT_PORT_ENABLE            BIT(0)

#define PLDA_PCIE_IDS_DW1                0x9c
#define PLDA_REVISION_ID_MASK            0xffU
#define PLDA_CLASS_BRIDGE_PCI            0x0604U
#define PLDA_CLASS_CODE_SHIFT            16

#define PLDA_PCI_MISC                    0xb4
#define PLDA_PHY_FUNCTION_DISABLE        BIT(15)

#define PLDA_PCIE_WINROM                 0xfc
#define PLDA_PREF_MEM_WIN_64_SUPPORT     BIT(3)

#define PLDA_PMSG_SUPPORT_RX             0x3f0
#define PLDA_PMSG_LTR_SUPPORT            BIT(2)

#define PLDA_LOCAL_CONFIG_OFFSET         0x1000
#define PCI_BASE_ADDRESS_0               0x10
#define PCI_BASE_ADDRESS_1               0x14


//GPIO26 controla PERST de PCIe0 y es activo a nivel bajo
#define PCIE0_PERST_GPIO                 26U

#define SYS_GPIO_DOEN                    0x000
#define SYS_GPIO_DOUT                    0x040
#define SYS_GPIO_FUNCTION_20_29          0x2a0

#define GPIO26_FUNCTION_SHIFT            18
#define GPIO26_FUNCTION_MASK             (3U << GPIO26_FUNCTION_SHIFT)

#define GPIO_DOUT_FIELD_MASK             0x7fU
#define GPIO_DOEN_FIELD_MASK             0x3fU

#define GPIO_OUTPUT_LOW                  0U
#define GPIO_OUTPUT_HIGH                 1U
#define GPIO_OUTPUT_ENABLE               0U


//Número de funciones físicas implementadas por el Root Port PLDA
#define PCIE_FUNCTION_COUNT              4U


//Indica si esta etapa terminó con el enlace físico activo
static int pcie0_link_active;


static inline uint32
mmio_read32(uint64 address)
{
  uint32 value;

  value = *(volatile uint32 *)address;

  //Evita que la CPU reordene la lectura MMIO
  __sync_synchronize();

  return value;
}


static inline void
mmio_write32(uint64 address, uint32 value)
{
  //Asegura que las escrituras anteriores han terminado
  __sync_synchronize();

  *(volatile uint32 *)address = value;

  //Asegura que la escritura MMIO termina antes de continuar
  __sync_synchronize();
}


static void
mmio_set_bits(uint64 address, uint32 bits)
{
  uint32 value;

  value = mmio_read32(address);
  mmio_write32(address, value | bits);
}


static void
mmio_clear_bits(uint64 address, uint32 bits)
{
  uint32 value;

  value = mmio_read32(address);
  mmio_write32(address, value & ~bits);
}


static void
mmio_update_bits(uint64 address, uint32 mask, uint32 value)
{
  uint32 old;

  old = mmio_read32(address);
  old &= ~mask;
  old |= value & mask;

  mmio_write32(address, old);
}


/*
Espera el número de microsegundos indicado utilizando el contador
time de RISC-V.

En el JH7110 cada microsegundo corresponde aproximadamente a cuatro
incrementos del contador.
*/
static void
vf2_pcie_delay_us(uint32 microseconds)
{
  uint64 start;
  uint64 ticks;

  start = r_time();
  ticks = (uint64)microseconds * JH7110_TICKS_PER_US;

  while((r_time() - start) < ticks)
    __asm__ volatile("nop");
}


static void
vf2_pcie_delay_ms(uint32 milliseconds)
{
  while(milliseconds > 0){
    vf2_pcie_delay_us(1000U);
    milliseconds--;
  }
}


static uint64
sys_clock_register(uint32 index)
{
  return VF2_SYS_CRG_BASE + ((uint64)index * 4U);
}


static uint64
stg_clock_register(uint32 index)
{
  return VF2_STG_CRG_BASE + ((uint64)index * 4U);
}


static void
vf2_pcie_enable_clock(uint64 address)
{
  mmio_set_bits(address, JH7110_CLK_ENABLE);
}


/*
Activa los relojes necesarios para acceder al GPIO de PERST y al
controlador PCIe0.

PCIE_SLV_MAIN es un reloj crítico compartido por los dos controladores
PCIe. Se mantiene habilitado junto a los tres relojes propios de PCIe0.
*/
static void
vf2_pcie0_enable_clocks(void)
{
  vf2_pcie_enable_clock(
    sys_clock_register(SYSCLK_NOC_BUS_STG_AXI));

  vf2_pcie_enable_clock(
    sys_clock_register(SYSCLK_IOMUX_APB));

  vf2_pcie_enable_clock(
    stg_clock_register(STGCLK_PCIE0_AXI_MST0));

  vf2_pcie_enable_clock(
    stg_clock_register(STGCLK_PCIE0_APB));

  vf2_pcie_enable_clock(
    stg_clock_register(STGCLK_PCIE0_TL));

  vf2_pcie_enable_clock(
    stg_clock_register(STGCLK_PCIE_SLV_MAIN));

  printf("pcie0: clocks enabled\n");

  printf("pcie0: noc=0x%x iomux=0x%x\n",
         mmio_read32(
           sys_clock_register(SYSCLK_NOC_BUS_STG_AXI)),
         mmio_read32(
           sys_clock_register(SYSCLK_IOMUX_APB)));

  printf("pcie0: axi=0x%x apb=0x%x tl=0x%x\n",
         mmio_read32(
           stg_clock_register(STGCLK_PCIE0_AXI_MST0)),
         mmio_read32(
           stg_clock_register(STGCLK_PCIE0_APB)),
         mmio_read32(
           stg_clock_register(STGCLK_PCIE0_TL)));
}


/*
Configura GPIO26 como salida GPIO.

Los registros DOUT y DOEN agrupan cuatro GPIO por palabra. Cada GPIO
utiliza un campo de ocho bits. En DOEN, el valor cero habilita la salida.
*/
static void
vf2_pcie0_configure_perst_gpio(void)
{
  uint32 offset;
  uint32 shift;
  uint32 value;
  uint32 mask;

  offset = 4U * (PCIE0_PERST_GPIO / 4U);
  shift = 8U * (PCIE0_PERST_GPIO % 4U);

  /*
  Se selecciona la función cero para utilizar el pin como GPIO normal.
  GPIO26 ocupa los bits 19:18 del registro 0x2a0.
  */
  mmio_update_bits(
    VF2_SYS_GPIO_BASE + SYS_GPIO_FUNCTION_20_29,
    GPIO26_FUNCTION_MASK,
    0);

  /*
  Se escribe primero el nivel bajo y después se habilita la salida.
  De este modo el VL805 permanece en reset durante la configuración.
  */
  mask = GPIO_DOUT_FIELD_MASK << shift;
  value = GPIO_OUTPUT_LOW << shift;

  mmio_update_bits(
    VF2_SYS_GPIO_BASE + SYS_GPIO_DOUT + offset,
    mask,
    value);

  mask = GPIO_DOEN_FIELD_MASK << shift;
  value = GPIO_OUTPUT_ENABLE << shift;

  mmio_update_bits(
    VF2_SYS_GPIO_BASE + SYS_GPIO_DOEN + offset,
    mask,
    value);

  printf("pcie0: GPIO26 configured as active-low PERST\n");
}


/*
Controla la señal PERST del dispositivo PCIe conectado.

asserted distinto de cero mantiene el VL805 en reset mediante nivel bajo.
asserted igual a cero libera el reset mediante nivel alto.
*/
static void
vf2_pcie0_set_perst(int asserted)
{
  uint32 offset;
  uint32 shift;
  uint32 value;
  uint32 mask;

  offset = 4U * (PCIE0_PERST_GPIO / 4U);
  shift = 8U * (PCIE0_PERST_GPIO % 4U);

  mask = GPIO_DOUT_FIELD_MASK << shift;

  if(asserted)
    value = GPIO_OUTPUT_LOW << shift;
  else
    value = GPIO_OUTPUT_HIGH << shift;

  mmio_update_bits(
    VF2_SYS_GPIO_BASE + SYS_GPIO_DOUT + offset,
    mask,
    value);

  printf("pcie0: PERST %s\n",
         asserted ? "asserted" : "deasserted");
}


/*
Configura el bloque Multi-PHY situado en 0x10210000 para que trabaje
en modo PCI Express.

El mismo tipo de PHY puede funcionar como PCIe o USB3. En PCIe0 debe
seleccionarse el modo PCIe, utilizarse un bus de ocho bits y mantenerse
desactivada la parte específica de USB3.
*/
static void
vf2_pcie0_configure_phy(void)
{
  uint32 before_mode;
  uint32 before_width;
  uint32 after_mode;
  uint32 after_width;

  before_mode =
    mmio_read32(
      VF2_STG_SYSCON_BASE + STG_PCIE_PHY_MODE_OFFSET);

  before_width =
    mmio_read32(
      VF2_STG_SYSCON_BASE + STG_PCIE_PHY_USB_OFFSET);

  printf("pcie0: PHY mode before=0x%x width before=0x%x\n",
         before_mode,
         before_width);

  //El valor cero en los bits 21:20 selecciona el modo PCIe
  mmio_update_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE_PHY_MODE_OFFSET,
    PCIE_PHY_MODE_MASK,
    0);

  //El bit 3 selecciona el ancho utilizado por la interfaz PCIe
  mmio_update_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE_PHY_USB_OFFSET,
    PCIE_PHY_BUS_WIDTH_MASK,
    PCIE_PHY_BUS_WIDTH_PCIE);

  //La habilitación específica de USB3 debe quedar desactivada
  mmio_clear_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE_PHY_USB_OFFSET,
    PCIE_PHY_ENABLE);

  //Se desconecta la ruta USB y se conecta el PHY al bloque PCIe
  mmio_clear_bits(
    VF2_SYS_SYSCON_BASE + SYS_PCIE_PHY_CONNECT_OFFSET,
    USB_PDRSTN_SPLIT);

  //También se desactiva la habilitación USB3 dentro del propio PHY
  mmio_clear_bits(
    VF2_PCIE0_PHY_BASE + PCIE_PHY_PLL_CONTROL_OFFSET,
    PCIE_PHY_ENABLE);

  /*
  Se aplican los valores de ajuste fino del PLL utilizados por el
  driver del JH7110 para el Multi-PHY de PCIe.
  */
  mmio_write32(
    VF2_PCIE0_PHY_BASE + PCIE_PHY_KVCO_LEVEL_OFFSET,
    PCIE_PHY_KVCO_LEVEL);

  mmio_write32(
    VF2_PCIE0_PHY_BASE + PCIE_PHY_KVCO_TUNE_OFFSET,
    PCIE_PHY_KVCO_TUNE);

  after_mode =
    mmio_read32(
      VF2_STG_SYSCON_BASE + STG_PCIE_PHY_MODE_OFFSET);

  after_width =
    mmio_read32(
      VF2_STG_SYSCON_BASE + STG_PCIE_PHY_USB_OFFSET);

  printf("pcie0: PHY mode after=0x%x width after=0x%x\n",
         after_mode,
         after_width);

  printf("pcie0: Multi-PHY configured for PCIe\n");
}


/*
Configura el Root Port y la fuente del reloj de referencia.

RP_NEP selecciona el funcionamiento como Root Port.
CKREF=2 selecciona la fuente de reloj utilizada por PCIe.
CLKREQ habilita el manejo de la señal de petición de reloj.
*/
static void
vf2_pcie0_configure_stg(void)
{
  mmio_set_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE0_RP_NEP_OFFSET,
    STG_SYSCON_K_RP_NEP);

  mmio_update_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE0_AW_OFFSET,
    STG_SYSCON_CKREF_SRC_MASK,
    STG_SYSCON_CKREF_SRC_PCIE);

  mmio_set_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE0_AW_OFFSET,
    STG_SYSCON_CLKREQ);

  printf("pcie0: STG root-port and reference clock configured\n");

  printf("pcie0: STG AW=0x%x RP_NEP=0x%x\n",
         mmio_read32(
           VF2_STG_SYSCON_BASE + STG_PCIE0_AW_OFFSET),
         mmio_read32(
           VF2_STG_SYSCON_BASE + STG_PCIE0_RP_NEP_OFFSET));
}


/*
Espera hasta que las seis líneas de reset de PCIe0 aparezcan
liberadas en el registro de estado del STG CRG.
*/
static int
vf2_pcie0_wait_resets(void)
{
  uint32 status;
  uint32 i;

  for(i = 0; i < 1000000U; i++){
    status =
      mmio_read32(VF2_STG_CRG_BASE + STG_RESET_STATUS);

    if((status & STGRST_PCIE0_MASK) == STGRST_PCIE0_MASK)
      return 0;

    __asm__ volatile("nop");
  }

  printf("pcie0: reset timeout status=0x%x\n",
         mmio_read32(
           VF2_STG_CRG_BASE + STG_RESET_STATUS));

  return -1;
}


/*
Libera los resets AXI, bridge, core y APB de PCIe0.
*/
static int
vf2_pcie0_deassert_resets(void)
{
  uint32 before;
  uint32 after;

  before =
    mmio_read32(VF2_STG_CRG_BASE + STG_RESET_ASSERT);

  printf("pcie0: reset assert before=0x%x\n",
         before);

  mmio_clear_bits(
    VF2_STG_CRG_BASE + STG_RESET_ASSERT,
    STGRST_PCIE0_MASK);

  if(vf2_pcie0_wait_resets() < 0)
    return -1;

  after =
    mmio_read32(VF2_STG_CRG_BASE + STG_RESET_ASSERT);

  printf("pcie0: reset assert after=0x%x status=0x%x\n",
         after,
         mmio_read32(
           VF2_STG_CRG_BASE + STG_RESET_STATUS));

  return 0;
}


/*
Selecciona la función física del Root Port que recibirán los accesos
posteriores a los registros PLDA.
*/
static void
vf2_pcie0_select_function(uint32 function)
{
  mmio_update_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE0_AR_OFFSET,
    STG_AXI4_SLVL_AR_MASK,
    STG_AXI4_SLVL_PHY_AR(function));

  mmio_update_bits(
    VF2_STG_SYSCON_BASE + STG_PCIE0_AW_OFFSET,
    STG_AXI4_SLVL_AW_MASK,
    STG_AXI4_SLVL_PHY_AW(function));
}


/*
Configura el bloque PLDA como Root Port PCI estándar.

El hardware presenta cuatro funciones físicas. Solo se mantiene activa
la función cero. Las funciones uno, dos y tres se deshabilitan.
*/
static void
vf2_pcie0_configure_root_port(void)
{
  uint32 function;
  uint32 value;

  for(function = 1; function < PCIE_FUNCTION_COUNT; function++){
    vf2_pcie0_select_function(function);

    mmio_set_bits(
      VF2_PCIE0_APB_BASE + PLDA_PCI_MISC,
      PLDA_PHY_FUNCTION_DISABLE);
  }

  //Se restauran los accesos a la función física cero
  vf2_pcie0_select_function(0);

  //Se habilita el funcionamiento como Root Port
  mmio_set_bits(
    VF2_PCIE0_APB_BASE + PLDA_GEN_SETTINGS,
    PLDA_ROOT_PORT_ENABLE);

  //El Root Complex no necesita utilizar BAR0 ni BAR1
  mmio_write32(
    VF2_PCIE0_APB_BASE +
    PLDA_LOCAL_CONFIG_OFFSET +
    PCI_BASE_ADDRESS_0,
    0);

  mmio_write32(
    VF2_PCIE0_APB_BASE +
    PLDA_LOCAL_CONFIG_OFFSET +
    PCI_BASE_ADDRESS_1,
    0);

  /*
  Se configura la clase PCI como puente PCI-to-PCI y se conserva
  el identificador de revisión proporcionado por el hardware.
  */
  value =
    mmio_read32(VF2_PCIE0_APB_BASE + PLDA_PCIE_IDS_DW1);

  value &= PLDA_REVISION_ID_MASK;
  value |= PLDA_CLASS_BRIDGE_PCI << PLDA_CLASS_CODE_SHIFT;

  mmio_write32(
    VF2_PCIE0_APB_BASE + PLDA_PCIE_IDS_DW1,
    value);

  /*
  Se desactiva temporalmente el reenvío LTR porque todavía no existe
  una dirección de reenvío válida configurada.
  */
  mmio_clear_bits(
    VF2_PCIE0_APB_BASE + PLDA_PMSG_SUPPORT_RX,
    PLDA_PMSG_LTR_SUPPORT);

  //Se habilita el soporte para ventanas prefetchable de 64 bits
  mmio_set_bits(
    VF2_PCIE0_APB_BASE + PLDA_PCIE_WINROM,
    PLDA_PREF_MEM_WIN_64_SUPPORT);

  printf("pcie0: PLDA Root Port configured\n");

  printf("pcie0: GEN=0x%x IDS=0x%x MISC=0x%x\n",
         mmio_read32(
           VF2_PCIE0_APB_BASE + PLDA_GEN_SETTINGS),
         mmio_read32(
           VF2_PCIE0_APB_BASE + PLDA_PCIE_IDS_DW1),
         mmio_read32(
           VF2_PCIE0_APB_BASE + PLDA_PCI_MISC));
}


/*
Comprueba el bit DATA_LINK_ACTIVE del STG syscon.

El driver oficial realiza diez intentos separados por aproximadamente
cien milisegundos. Esta primera etapa utiliza la misma estrategia.
*/
static int
vf2_pcie0_wait_for_link(void)
{
  uint32 status;
  uint32 attempt;

  for(attempt = 0;
      attempt < PCIE_LINK_WAIT_RETRIES;
      attempt++){

    status =
      mmio_read32(
        VF2_STG_SYSCON_BASE + STG_PCIE0_LNKSTA_OFFSET);

    printf("pcie0: link attempt=%d status=0x%x\n",
           (int)(attempt + 1U),
           status);

    if(status & STG_DATA_LINK_ACTIVE){
      pcie0_link_active = 1;
      printf("pcie0: port link up\n");
      return 0;
    }

    vf2_pcie_delay_ms(PCIE_LINK_WAIT_MS);
  }

  pcie0_link_active = 0;

  printf("pcie0: port link down\n");

  return -1;
}


int
vf2_pcie0_link_up(void)
{
  return pcie0_link_active;
}


int
vf2_pcie0_init(void)
{
  pcie0_link_active = 0;

  printf("\n");
  printf("========================================\n");
  printf(" JH7110 PCIE0 DRIVER - STAGE 1\n");
  printf("========================================\n");

  printf("pcie0: APB base=%p\n",
         (void *)VF2_PCIE0_APB_BASE);

  printf("pcie0: PHY base=%p\n",
         (void *)VF2_PCIE0_PHY_BASE);

  /*
  El VL805 debe permanecer en reset mientras se configuran el PHY,
  los relojes, los resets y el Root Port.
  */
  vf2_pcie0_enable_clocks();
  vf2_pcie0_configure_perst_gpio();
  vf2_pcie0_set_perst(1);

  vf2_pcie0_configure_phy();
  vf2_pcie0_configure_stg();

  if(vf2_pcie0_deassert_resets() < 0){
    printf("pcie0: failed to release controller resets\n");
    return -1;
  }

  vf2_pcie0_configure_root_port();

  /*
  PERST debe permanecer activo durante al menos cien milisegundos
  después de que la alimentación y los relojes sean estables.
  */
  printf("pcie0: holding PERST for 100 ms\n");
  vf2_pcie_delay_ms(100U);

  vf2_pcie0_set_perst(0);

  /*
  Después de liberar PERST se esperan otros cien milisegundos antes
  de comprobar o enviar peticiones de configuración al dispositivo.
  */
  printf("pcie0: waiting 100 ms after PERST release\n");
  vf2_pcie_delay_ms(100U);

  if(vf2_pcie0_wait_for_link() < 0){
    printf("pcie0: stage 1 failed\n");
    printf("========================================\n");
    return -1;
  }

  printf("pcie0: stage 1 completed\n");
  printf("========================================\n");

  return 0;
}
