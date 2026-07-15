#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

/*
 * ================================================================
 * StarFive JH7110 HDMI driver — V4
 * ================================================================
 *
 * Resolución:
 *   1920x1080, 60 Hz
 *
 * Formato de framebuffer:
 *   32 bits por píxel
 *
 * Framebuffer físico:
 *   FRAMEBUFFER_PA, definido en memlayout.h
 *
 * Esta versión:
 *   - enciende el dominio VOUT;
 *   - habilita clocks;
 *   - libera resets;
 *   - genera un patrón de prueba;
 *   - programa DC8200;
 *   - programa el PLL HDMI;
 *   - enciende PHY y TMDS.
 */

/*
 * ------------------------------------------------
 * Modo de vídeo
 * ------------------------------------------------
 */

#define FB_WIDTH                 1920U
#define FB_HEIGHT                1080U
#define FB_BYTES_PER_PIXEL       4U
#define FB_STRIDE                (FB_WIDTH * FB_BYTES_PER_PIXEL)

/*
 * El contador de tiempo del JH7110 funciona a 4 MHz.
 */
#define JH7110_TIMEBASE_HZ       4000000UL

/*
 * ------------------------------------------------
 * PMU
 * ------------------------------------------------
 */

#define PMU_SW_TURN_ON           0x000cU
#define PMU_SW_ENCOURAGE         0x0044U
#define PMU_CURRENT_POWER_MODE   0x0080U
#define PMU_CURRENT_SEQ_STATE    0x0084U
#define PMU_EVENT_STATUS         0x0088U

#define PMU_VOUT_BIT             (1U << 4)

/*
 * ------------------------------------------------
 * Formato de registros de clocks
 * ------------------------------------------------
 */

#define CLK_ENABLE_BIT           (1U << 31)

/*
 * Los mux utilizados para dc_pix0/dc_pix1 tienen la selección
 * de parent en el bit 24.
 */
#define CLK_PARENT_SELECT_BIT    (1U << 24)

/*
 * ------------------------------------------------
 * SYS CRG
 * Base definida como SYS_CRG_BASE en memlayout.h.
 * ------------------------------------------------
 */

#define SYS_CLK_VOUT_SRC             0x00e8U
#define SYS_CLK_VOUT_AXI             0x00ecU
#define SYS_CLK_NOC_DISP_AXI         0x00f0U
#define SYS_CLK_VOUT_TOP_AHB         0x00f4U
#define SYS_CLK_VOUT_TOP_AXI         0x00f8U
#define SYS_CLK_HDMI_MCLK_PARENT     0x00fcU

#define SYS_RESET_ASSERT_BASE        0x02f8U
#define SYS_RESET_STATUS_BASE        0x0308U

#define SYS_RESET_ID_NOC_DISP        26U
#define SYS_RESET_ID_VOUT_SRC        43U

/*
 * ------------------------------------------------
 * VOUT CRG
 * Base definida como VOUT_CRG_BASE en memlayout.h.
 * ------------------------------------------------
 */

#define VOUT_CLK_APB                 0x0000U
#define VOUT_CLK_DC_PIX_DIV          0x0004U

#define VOUT_CLK_DC_AXI              0x0010U
#define VOUT_CLK_DC_CORE             0x0014U
#define VOUT_CLK_DC_AHB              0x0018U
#define VOUT_CLK_DC_PIX0             0x001cU
#define VOUT_CLK_DC_PIX1             0x0020U

#define VOUT_CLK_HDMI_MCLK           0x003cU
#define VOUT_CLK_HDMI_BCLK           0x0040U
#define VOUT_CLK_HDMI_SYS            0x0044U

#define VOUT_RESET_ASSERT_BASE       0x0048U
#define VOUT_RESET_STATUS_BASE       0x004cU

#define VOUT_RESET_ID_DC_AXI         0U
#define VOUT_RESET_ID_DC_AHB         1U
#define VOUT_RESET_ID_DC_CORE        2U
#define VOUT_RESET_ID_HDMI_TX        9U

/*
 * ------------------------------------------------
 * Acceso al framebuffer
 * ------------------------------------------------
 */

static volatile uint32 *const framebuffer =
  (volatile uint32 *)(uint64)FRAMEBUFFER_PA;

/*
 * ------------------------------------------------
 * Helpers MMIO
 * ------------------------------------------------
 */

static inline uint32
mmio_read32(uint64 address)
{
  uint32 value;

  asm volatile("fence iorw, iorw" ::: "memory");

  value = *(volatile uint32 *)address;

  asm volatile("fence iorw, iorw" ::: "memory");

  return value;
}

static inline void
mmio_write32(uint64 address, uint32 value)
{
  asm volatile("fence iorw, iorw" ::: "memory");

  *(volatile uint32 *)address = value;

  asm volatile("fence iorw, iorw" ::: "memory");
}

static inline void
mmio_update32(uint64 address,
              uint32 clear_mask,
              uint32 set_mask)
{
  uint32 value;

  value = mmio_read32(address);
  value &= ~clear_mask;
  value |= set_mask;

  mmio_write32(address, value);
}

static void
delay_loop(uint64 iterations)
{
  while(iterations--)
    asm volatile("nop");
}

/*
 * Lee el contador TIME del procesador.
 */
static inline uint64
hdmi_read_time(void)
{
  uint64 value;

  asm volatile("rdtime %0" : "=r"(value));

  return value;
}

/*
 * Retardo basado en el contador de 4 MHz.
 */
static void
delay_ms(uint32 milliseconds)
{
  uint64 start;
  uint64 ticks;

  start = hdmi_read_time();

  ticks =
    ((uint64)JH7110_TIMEBASE_HZ * milliseconds) / 1000U;

  while((hdmi_read_time() - start) < ticks)
    asm volatile("nop");
}

/*
 * ------------------------------------------------
 * Helpers de clocks
 * ------------------------------------------------
 */

static void
clock_gate_enable(uint64 address)
{
  mmio_update32(address, 0, CLK_ENABLE_BIT);
}

static void
clock_divider_set(uint64 address,
                  uint32 divider_mask,
                  uint32 divider)
{
  mmio_update32(address,
                divider_mask,
                divider & divider_mask);
}

/*
 * Selecciona parent 0 y habilita el gate del clock composite.
 */
static void
clock_parent0_enable(uint64 address)
{
  mmio_update32(address,
                CLK_PARENT_SELECT_BIT,
                CLK_ENABLE_BIT);
}

/*
 * ------------------------------------------------
 * PMU
 * ------------------------------------------------
 */

static void
pmu_dump_regs(const char *tag)
{
  uint32 turn_on;
  uint32 encourage;
  uint32 current;
  uint32 sequence;
  uint32 events;

  turn_on =
    mmio_read32(JH7110_PMU_BASE +
                PMU_SW_TURN_ON);

  encourage =
    mmio_read32(JH7110_PMU_BASE +
                PMU_SW_ENCOURAGE);

  current =
    mmio_read32(JH7110_PMU_BASE +
                PMU_CURRENT_POWER_MODE);

  sequence =
    mmio_read32(JH7110_PMU_BASE +
                PMU_CURRENT_SEQ_STATE);

  events =
    mmio_read32(JH7110_PMU_BASE +
                PMU_EVENT_STATUS);

  printf("pmu %s:\n", tag);

  printf("  turn_on=0x%x encourage=0x%x\n",
         turn_on,
         encourage);

  printf("  current=0x%x vout=%d "
         "sequence=0x%x events=0x%x\n",
         current,
         (current & PMU_VOUT_BIT) != 0,
         sequence,
         events);
}

static int
vout_power_on(void)
{
  uint32 current;

  pmu_dump_regs("before");

  current =
    mmio_read32(JH7110_PMU_BASE +
                PMU_CURRENT_POWER_MODE);

  if(current & PMU_VOUT_BIT){
    printf("hdmi: VOUT power domain was already ON\n");
    return 0;
  }

  printf("hdmi: requesting VOUT power-on\n");

  mmio_write32(JH7110_PMU_BASE +
               PMU_SW_TURN_ON,
               PMU_VOUT_BIT);

  mmio_write32(JH7110_PMU_BASE +
               PMU_SW_ENCOURAGE,
               0xff);

  mmio_write32(JH7110_PMU_BASE +
               PMU_SW_ENCOURAGE,
               0x05);

  mmio_write32(JH7110_PMU_BASE +
               PMU_SW_ENCOURAGE,
               0x50);

  for(uint32 attempt = 0;
      attempt < 100000;
      attempt++){

    current =
      mmio_read32(JH7110_PMU_BASE +
                  PMU_CURRENT_POWER_MODE);

    if(current & PMU_VOUT_BIT){
      pmu_dump_regs("after");
      printf("hdmi: VOUT power domain is ON\n");
      return 0;
    }

    delay_loop(1000);
  }

  pmu_dump_regs("timeout");
  printf("hdmi: VOUT power-on timed out\n");

  return -1;
}

/*
 * ------------------------------------------------
 * Resets
 * ------------------------------------------------
 *
 * assert:
 *   bit 1 = reset activo
 *   bit 0 = reset liberado
 *
 * status:
 *   bit 1 = reset liberado
 */

static int
reset_deassert_id(uint64 controller_base,
                  uint32 assert_base_offset,
                  uint32 status_base_offset,
                  uint32 reset_id,
                  const char *name)
{
  uint32 bank;
  uint32 bit;
  uint32 mask;
  uint64 assert_address;
  uint64 status_address;
  uint32 assert_value;
  uint32 status_value;

  bank = reset_id / 32U;
  bit = reset_id % 32U;
  mask = 1U << bit;

  assert_address =
    controller_base +
    assert_base_offset +
    ((uint64)bank * 4U);

  status_address =
    controller_base +
    status_base_offset +
    ((uint64)bank * 4U);

  assert_value = mmio_read32(assert_address);
  assert_value &= ~mask;

  mmio_write32(assert_address, assert_value);

  for(uint32 attempt = 0;
      attempt < 100000;
      attempt++){

    status_value = mmio_read32(status_address);

    if(status_value & mask){
      printf("hdmi: reset %s deasserted "
             "assert=0x%x status=0x%x\n",
             name,
             mmio_read32(assert_address),
             status_value);

      return 0;
    }

    delay_loop(100);
  }

  printf("hdmi: reset %s timeout "
         "assert=0x%x status=0x%x mask=0x%x\n",
         name,
         mmio_read32(assert_address),
         mmio_read32(status_address),
         mask);

  return -1;
}

/*
 * ------------------------------------------------
 * Diagnóstico de clocks
 * ------------------------------------------------
 */

static void
dump_sys_clocks(void)
{
  printf("hdmi: SYS clocks:\n");

  printf("  e8(vout-src)  =0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE + SYS_CLK_VOUT_SRC));

  printf("  ec(vout-axi)  =0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE + SYS_CLK_VOUT_AXI));

  printf("  f0(disp-axi)  =0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE + SYS_CLK_NOC_DISP_AXI));

  printf("  f4(vout-ahb)  =0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE + SYS_CLK_VOUT_TOP_AHB));

  printf("  f8(vout-axi-g)=0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE + SYS_CLK_VOUT_TOP_AXI));

  printf("  fc(hdmi-mclk) =0x%lx\n",
         (uint64)mmio_read32(
           SYS_CRG_BASE +
           SYS_CLK_HDMI_MCLK_PARENT));
}

static void
dump_vout_clocks(void)
{
  printf("hdmi: VOUT clocks:\n");

  printf("  00(apb-div)   =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_APB));

  printf("  04(pixel-div) =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_PIX_DIV));

  printf("  10(dc-axi)    =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_AXI));

  printf("  14(dc-core)   =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_CORE));

  printf("  18(dc-ahb)    =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_AHB));

  printf("  1c(dc-pix0)   =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_PIX0));

  printf("  20(dc-pix1)   =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_DC_PIX1));

  printf("  3c(hdmi-mclk) =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_HDMI_MCLK));

  printf("  40(hdmi-bclk) =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_HDMI_BCLK));

  printf("  44(hdmi-sys)  =0x%lx\n",
         (uint64)mmio_read32(
           VOUT_CRG_BASE + VOUT_CLK_HDMI_SYS));

  printf("  reset assert=0x%x status=0x%x\n",
         mmio_read32(VOUT_CRG_BASE +
                     VOUT_RESET_ASSERT_BASE),
         mmio_read32(VOUT_CRG_BASE +
                     VOUT_RESET_STATUS_BASE));
}

/*
 * ------------------------------------------------
 * Inicialización de clocks y resets
 * ------------------------------------------------
 */

static int
vout_enable_clocks_and_resets(void)
{
  printf("hdmi: configuring SYS VOUT clocks\n");

  /*
   * Divisor VOUT_AXI = 1.
   */
  clock_divider_set(
    SYS_CRG_BASE + SYS_CLK_VOUT_AXI,
    0x7U,
    1U
  );

  /*
   * Camino NOC display.
   */
  clock_gate_enable(
    SYS_CRG_BASE + SYS_CLK_NOC_DISP_AXI
  );

  if(reset_deassert_id(
       SYS_CRG_BASE,
       SYS_RESET_ASSERT_BASE,
       SYS_RESET_STATUS_BASE,
       SYS_RESET_ID_NOC_DISP,
       "noc-disp") < 0){
    return -1;
  }

  /*
   * Clocks superiores del dominio VOUT.
   */
  clock_gate_enable(
    SYS_CRG_BASE + SYS_CLK_VOUT_SRC
  );

  clock_gate_enable(
    SYS_CRG_BASE + SYS_CLK_VOUT_TOP_AXI
  );

  clock_gate_enable(
    SYS_CRG_BASE + SYS_CLK_VOUT_TOP_AHB
  );

  clock_gate_enable(
    SYS_CRG_BASE + SYS_CLK_HDMI_MCLK_PARENT
  );

  if(reset_deassert_id(
       SYS_CRG_BASE,
       SYS_RESET_ASSERT_BASE,
       SYS_RESET_STATUS_BASE,
       SYS_RESET_ID_VOUT_SRC,
       "vout-src") < 0){
    return -1;
  }

  delay_ms(1);

  printf("hdmi: configuring local VOUT clocks\n");

  /*
   * APB local: divisor 1.
   */
  clock_divider_set(
    VOUT_CRG_BASE + VOUT_CLK_APB,
    0x1fU,
    1U
  );

  /*
   * Pixel clock:
   *
   *   PLL2 = 1.188 GHz
   *   1.188 GHz / 8 = 148.5 MHz
   *
   * El divisor del JH7110 es one-based, por lo que se escribe 8.
   */
  clock_divider_set(
    VOUT_CRG_BASE + VOUT_CLK_DC_PIX_DIV,
    0x3fU,
    8U
  );

  /*
   * Selecciona el pixel clock interno como parent y activa
   * los dos clocks de salida del DC8200.
   */
  clock_parent0_enable(
    VOUT_CRG_BASE + VOUT_CLK_DC_PIX0
  );

  clock_parent0_enable(
    VOUT_CRG_BASE + VOUT_CLK_DC_PIX1
  );

  /*
   * Buses y core DC8200.
   */
  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_DC_AXI
  );

  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_DC_CORE
  );

  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_DC_AHB
  );

  /*
   * Clocks HDMI.
   */
  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_HDMI_SYS
  );

  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_HDMI_MCLK
  );

  clock_gate_enable(
    VOUT_CRG_BASE + VOUT_CLK_HDMI_BCLK
  );

  delay_ms(1);

  /*
   * Resets del DC8200.
   */
  if(reset_deassert_id(
       VOUT_CRG_BASE,
       VOUT_RESET_ASSERT_BASE,
       VOUT_RESET_STATUS_BASE,
       VOUT_RESET_ID_DC_AXI,
       "dc8200-axi") < 0){
    return -1;
  }

  if(reset_deassert_id(
       VOUT_CRG_BASE,
       VOUT_RESET_ASSERT_BASE,
       VOUT_RESET_STATUS_BASE,
       VOUT_RESET_ID_DC_CORE,
       "dc8200-core") < 0){
    return -1;
  }

  if(reset_deassert_id(
       VOUT_CRG_BASE,
       VOUT_RESET_ASSERT_BASE,
       VOUT_RESET_STATUS_BASE,
       VOUT_RESET_ID_DC_AHB,
       "dc8200-ahb") < 0){
    return -1;
  }

  /*
   * Reset del transmisor HDMI.
   */
  if(reset_deassert_id(
       VOUT_CRG_BASE,
       VOUT_RESET_ASSERT_BASE,
       VOUT_RESET_STATUS_BASE,
       VOUT_RESET_ID_HDMI_TX,
       "hdmi-tx") < 0){
    return -1;
  }

  delay_ms(5);

  dump_sys_clocks();
  dump_vout_clocks();

  return 0;
}

/*
 * ------------------------------------------------
 * Patrón del framebuffer
 * ------------------------------------------------
 */

static void
framebuffer_test_pattern(void)
{
  uint32 color;

  printf("hdmi: drawing framebuffer test pattern\n");

  for(uint32 y = 0; y < FB_HEIGHT; y++){
    for(uint32 x = 0; x < FB_WIDTH; x++){

      /*
       * Ocho barras verticales.
       */
      if(x < FB_WIDTH / 8U)
        color = 0x00ffffffU;  /* blanco */
      else if(x < (FB_WIDTH * 2U) / 8U)
        color = 0x00ffff00U;  /* amarillo */
      else if(x < (FB_WIDTH * 3U) / 8U)
        color = 0x0000ffffU;  /* cian */
      else if(x < (FB_WIDTH * 4U) / 8U)
        color = 0x0000ff00U;  /* verde */
      else if(x < (FB_WIDTH * 5U) / 8U)
        color = 0x00ff00ffU;  /* magenta */
      else if(x < (FB_WIDTH * 6U) / 8U)
        color = 0x00ff0000U;  /* rojo */
      else if(x < (FB_WIDTH * 7U) / 8U)
        color = 0x000000ffU;  /* azul */
      else
        color = 0x00000000U;  /* negro */

      framebuffer[y * FB_WIDTH + x] = color;
    }
  }

  /*
   * Ordena las escrituras antes de activar el consumidor DMA.
   *
   * Esto no constituye por sí solo una limpieza explícita de caché;
   * la coherencia del framebuffer se comprobará con el patrón.
   */
  asm volatile("fence rw, rw" ::: "memory");

  printf("hdmi: framebuffer pattern ready\n");
}

/*
 * ------------------------------------------------
 * DC8200
 * ------------------------------------------------
 */

static void
dc8200_configure_1080p(void)
{
  printf("hdmi: programming DC8200 for 1920x1080\n");

  mmio_write32(DC8200_BASE + 0x0014,
               0xc0001fff);

  mmio_write32(DC8200_BASE + 0x1a38,
               0x000000e8);

  mmio_write32(DC8200_BASE + 0x1cc0,
               0x00002000);

  mmio_write32(DC8200_BASE + 0x24d8,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x24e0,
               0x04380780);

  mmio_write32(DC8200_BASE + 0x1810,
               0x04380780);

  /*
   * Dirección y stride del framebuffer.
   */
  mmio_write32(DC8200_BASE + 0x1400,
               (uint32)FRAMEBUFFER_PA);

  mmio_write32(DC8200_BASE + 0x1408,
               FB_STRIDE);

  mmio_write32(DC8200_BASE + 0x1ce8,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x2510,
               0x0000a9a3);

  mmio_write32(DC8200_BASE + 0x2508,
               0x2c4e6f06);

  mmio_write32(DC8200_BASE + 0x2500,
               0xe6daec4f);

  mmio_write32(DC8200_BASE + 0x1518,
               0x18220000);

  mmio_write32(DC8200_BASE + 0x1cc0,
               0x00003000);

  mmio_write32(DC8200_BASE + 0x1cc4,
               0x00030000);

  /*
   * Configuración del panel/capa primaria.
   */
  mmio_write32(DC8200_BASE + 0x1540,
               0x00050c1a);

  mmio_write32(DC8200_BASE + 0x2540,
               0x00000001);

  mmio_write32(DC8200_BASE + 0x1540,
               0x00050c1a);

  mmio_write32(DC8200_BASE + 0x1544,
               0x4016120c);

  mmio_write32(DC8200_BASE + 0x2544,
               0x00000002);

  mmio_write32(DC8200_BASE + 0x1544,
               0x4016120c);

  mmio_write32(DC8200_BASE + 0x1548,
               0x001b1208);

  mmio_write32(DC8200_BASE + 0x2548,
               0x00000004);

  mmio_write32(DC8200_BASE + 0x1548,
               0x001b1208);

  mmio_write32(DC8200_BASE + 0x154c,
               0x0016110e);

  mmio_write32(DC8200_BASE + 0x254c,
               0x00000005);

  mmio_write32(DC8200_BASE + 0x154c,
               0x0016110e);

  mmio_write32(DC8200_BASE + 0x2518,
               0x00000001);

  mmio_write32(DC8200_BASE + 0x1a28,
               0x00000000);

  /*
   * Timings 1920x1080p60.
   */
  mmio_write32(DC8200_BASE + 0x1430,
               0x08980780);

  mmio_write32(DC8200_BASE + 0x1438,
               0xc40207d8);

  mmio_write32(DC8200_BASE + 0x1440,
               0x04650438);

  mmio_write32(DC8200_BASE + 0x1448,
               0xc220843c);

  mmio_write32(DC8200_BASE + 0x14b0,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x1cd0,
               0x000000e2);

  mmio_write32(DC8200_BASE + 0x14d0,
               0x000000af);

  mmio_write32(DC8200_BASE + 0x14b8,
               0x00000005);

  mmio_write32(DC8200_BASE + 0x1528,
               0x8dd0b774);

  mmio_write32(DC8200_BASE + 0x1418,
               0x00001111);

  mmio_write32(DC8200_BASE + 0x1410,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x2518,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x1468,
               0x00000006);

  mmio_write32(DC8200_BASE + 0x1484,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x1468,
               0x00000006);

  mmio_write32(DC8200_BASE + 0x24e8,
               0x00011b25);

  mmio_write32(DC8200_BASE + 0x24fc,
               0x00000000);

  mmio_write32(DC8200_BASE + 0x24e8,
               0x00011b25);

  /*
   * Commit de la configuración.
   */
  mmio_write32(DC8200_BASE + 0x1ccc,
               0x00000001);

  asm volatile("fence iorw, iorw" ::: "memory");

  delay_ms(5);
}

static int
dc8200_readback(void)
{
  uint32 revision;
  uint32 chip_id;
  uint32 framebuffer_address;
  uint32 stride;
  uint32 horizontal;
  uint32 vertical;
  uint32 commit;

  revision =
    mmio_read32(DC8200_BASE + 0x0024);

  chip_id =
    mmio_read32(DC8200_BASE + 0x0030);

  framebuffer_address =
    mmio_read32(DC8200_BASE + 0x1400);

  stride =
    mmio_read32(DC8200_BASE + 0x1408);

  horizontal =
    mmio_read32(DC8200_BASE + 0x1430);

  vertical =
    mmio_read32(DC8200_BASE + 0x1440);

  commit =
    mmio_read32(DC8200_BASE + 0x1ccc);

  printf("hdmi: DC8200 revision=0x%x chip_id=0x%x\n",
         revision,
         chip_id);

  printf("hdmi: DC8200 readback "
         "fb=0x%x stride=0x%x "
         "h=0x%x v=0x%x commit=0x%x\n",
         framebuffer_address,
         stride,
         horizontal,
         vertical,
         commit);

  /*
   * El registro de commit puede limpiarse automáticamente,
   * por lo que no se verifica su valor.
   */
  if(framebuffer_address != (uint32)FRAMEBUFFER_PA){
    printf("hdmi: DC8200 framebuffer address mismatch\n");
    return -1;
  }

  if(stride != FB_STRIDE){
    printf("hdmi: DC8200 stride mismatch\n");
    return -1;
  }

  if(horizontal == 0 || vertical == 0){
    printf("hdmi: DC8200 timing readback failed\n");
    return -1;
  }

  return 0;
}

/*
 * ------------------------------------------------
 * HDMI TX
 * ------------------------------------------------
 *
 * Los registros lógicos HDMI están separados físicamente
 * cuatro bytes.
 */

static uint32
hdmi_read(uint32 register_index)
{
  return mmio_read32(
    HDMI_TX_BASE +
    ((uint64)register_index * 4U)
  );
}

static void
hdmi_write(uint32 register_index,
           uint32 value)
{
  mmio_write32(
    HDMI_TX_BASE +
    ((uint64)register_index * 4U),
    value
  );
}

struct hdmi_reg_value {
  uint32 reg;
  uint32 value;
};

/*
 * Tabla oficial para 1920x1080p60 usando la rama
 * de referencia que no define REF_CLK_27M.
 */
static const struct hdmi_reg_value
hdmi_pll_1080p60[] = {
  {0x1a0, 0x01},
  {0x1aa, 0x0f},
  {0x1a1, 0x01},
  {0x1a2, 0xf0},
  {0x1a3, 0x63},
  {0x1a4, 0x15},
  {0x1a5, 0x41},
  {0x1a6, 0x42},
  {0x1ab, 0x01},
  {0x1ac, 0x14},
  {0x1ad, 0x01},
  {0x1aa, 0x0e},
  {0x1a0, 0x00},
};

static void
hdmi_tx_ctrl_1080p(void)
{
  hdmi_write(0x09f, 0x06);
  hdmi_write(0x0a0, 0x82);
  hdmi_write(0x0a2, 0x0d);
  hdmi_write(0x0a3, 0x00);
  hdmi_write(0x0a4, 0x00);
  hdmi_write(0x0a5, 0x08);
  hdmi_write(0x0a6, 0x70);

  /*
   * CEA VIC 16: 1920x1080p60.
   */
  hdmi_write(0x0a7, 16);

  /*
   * Modo normal; 0 sería BIST.
   */
  hdmi_write(0x00c9, 0x10);
}

static int
hdmi_wait_pll_lock(void)
{
  uint64 start;
  uint64 timeout_ticks;
  uint32 prepll;
  uint32 postpll;

  start = hdmi_read_time();

  /*
   * Timeout de 500 ms.
   */
  timeout_ticks =
    ((uint64)JH7110_TIMEBASE_HZ * 500U) / 1000U;

  do {
    prepll = hdmi_read(0x1a9);
    postpll = hdmi_read(0x1af);

    if((prepll & 1U) && (postpll & 1U)){
      printf("hdmi: PLL locked "
             "prepll=0x%x postpll=0x%x\n",
             prepll,
             postpll);

      return 0;
    }
  } while((hdmi_read_time() - start) < timeout_ticks);

  prepll = hdmi_read(0x1a9);
  postpll = hdmi_read(0x1af);

  printf("hdmi: PLL lock timeout "
         "prepll=0x%x postpll=0x%x\n",
         prepll,
         postpll);

  return -1;
}

static void
hdmi_pll_dump(void)
{
  printf("hdmi: PLL readback "
         "1a0=%x 1aa=%x 1a1=%x 1a2=%x "
         "1a3=%x 1a4=%x 1a5=%x 1a6=%x\n",
         hdmi_read(0x1a0),
         hdmi_read(0x1aa),
         hdmi_read(0x1a1),
         hdmi_read(0x1a2),
         hdmi_read(0x1a3),
         hdmi_read(0x1a4),
         hdmi_read(0x1a5),
         hdmi_read(0x1a6));
}

static int
hdmi_enable_1080p(void)
{
  uint32 control_1b0;
  uint32 count;

  printf("hdmi: programming HDMI TX for 1080p60\n");

  /*
   * Habilita la lógica de detección/control utilizada
   * por el driver StarFive.
   */
  control_1b0 = hdmi_read(0x1b0);
  control_1b0 |= 0x04;

  hdmi_write(0x1b0, control_1b0);
  hdmi_write(0x1cc, 0x0f);

  /*
   * PHY apagado durante la programación.
   */
  hdmi_write(0x000, 0x63);

  /*
   * Programa el PLL.
   */
  count =
    sizeof(hdmi_pll_1080p60) /
    sizeof(hdmi_pll_1080p60[0]);

  for(uint32 i = 0; i < count; i++){
    hdmi_write(hdmi_pll_1080p60[i].reg,
               hdmi_pll_1080p60[i].value);
  }

  /*
   * Programa VIC, formato y modo normal.
   */
  hdmi_tx_ctrl_1080p();

  asm volatile("fence iorw, iorw" ::: "memory");

  hdmi_pll_dump();

  printf("hdmi: control readback "
         "1b0=0x%x 1cc=0x%x 1cd=0x%x\n",
         hdmi_read(0x1b0),
         hdmi_read(0x1cc),
         hdmi_read(0x1cd));

  /*
   * Espera a que ambos PLL indiquen lock.
   */
  if(hdmi_wait_pll_lock() < 0)
    return -1;

  /*
   * LDO y serializer.
   */
  hdmi_write(0x1b4, 0x07);
  hdmi_write(0x1be, 0x70);

  /*
   * PHY ON.
   */
  hdmi_write(0x000, 0x61);

  /*
   * TMDS ON.
   */
  hdmi_write(0x1b2, 0x8f);

  /*
   * El driver StarFive espera 50 ms después de habilitar TMDS.
   */
  delay_ms(50);

  /*
   * Sincronización de datos.
   */
  hdmi_write(0x0ce, 0x00);
  hdmi_write(0x0ce, 0x01);

  asm volatile("fence iorw, iorw" ::: "memory");

  printf("hdmi: PHY/TMDS readback "
         "phy=0x%x ldo=0x%x serializer=0x%x "
         "tmds=0x%x sync=0x%x\n",
         hdmi_read(0x000),
         hdmi_read(0x1b4),
         hdmi_read(0x1be),
         hdmi_read(0x1b2),
         hdmi_read(0x0ce));

  printf("hdmi: PHY, TMDS and data sync enabled\n");

  return 0;
}

/*
 * ------------------------------------------------
 * Entrada pública
 * ------------------------------------------------
 */

void
hdmi_init(void)
{
  printf("\n");
  printf("========================================\n");
  printf(" JH7110 HDMI DRIVER V4 - 1080p60 TEST\n");
  printf("========================================\n");

  printf("hdmi: framebuffer=%p size=0x%lx "
         "resolution=%dx%d stride=%d\n",
         (void *)(uint64)FRAMEBUFFER_PA,
         (uint64)FRAMEBUFFER_SIZE,
         (int)FB_WIDTH,
         (int)FB_HEIGHT,
         (int)FB_STRIDE);

  /*
   * 1. Alimentación.
   */
  if(vout_power_on() < 0){
    printf("hdmi: initialization failed at PMU\n");
    return;
  }

  /*
   * 2. Clocks y resets.
   */
  if(vout_enable_clocks_and_resets() < 0){
    printf("hdmi: initialization failed at clocks/resets\n");
    return;
  }

  /*
   * 3. Patrón en memoria.
   */
  framebuffer_test_pattern();

  /*
   * 4. Controlador de pantalla.
   */
  dc8200_configure_1080p();

  if(dc8200_readback() < 0){
    printf("hdmi: initialization failed at DC8200\n");
    return;
  }

  /*
   * 5. Transmisor HDMI.
   */
  if(hdmi_enable_1080p() < 0){
    printf("hdmi: initialization failed at HDMI PLL\n");
    return;
  }

  printf("========================================\n");
  printf(" HDMI V4 INITIALIZATION COMPLETED\n");
  printf("========================================\n");
}