#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"

#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#include "defs.h"
#include "sdcard.h"

#define SD_SECTOR_SIZE 512
#define SECTORS_PER_XV6_BLOCK (BSIZE / SD_SECTOR_SIZE)

// Partición 2 de la microSD:
// First sector: 264192, este es el LBA (el bloque logico) donde inicia la segunda particion
#define XV6_FS_START_LBA 264192UL

// Controlador microSD de VisionFive 2 / JH7110.
#define SD_BASE MMC1

// Offsets DesignWare MMC.
#define SDMMC_CTRL      0x000
#define SDMMC_PWREN     0x004
#define SDMMC_CLKDIV    0x008
#define SDMMC_CLKSRC    0x00c
#define SDMMC_CLKENA    0x010
#define SDMMC_TMOUT     0x014
#define SDMMC_CTYPE     0x018
#define SDMMC_BLKSIZ    0x01c
#define SDMMC_BYTCNT    0x020
#define SDMMC_INTMASK   0x024
#define SDMMC_CMDARG    0x028
#define SDMMC_CMD       0x02c
#define SDMMC_RESP0     0x030
#define SDMMC_RESP1     0x034
#define SDMMC_RESP2     0x038
#define SDMMC_RESP3     0x03c
#define SDMMC_MINTSTS   0x040
#define SDMMC_RINTSTS   0x044
#define SDMMC_STATUS    0x048
#define SDMMC_FIFOTH    0x04c
#define SDMMC_CDETECT   0x050
#define SDMMC_VERID     0x06c
#define SDMMC_HCON      0x070

// FIFO:
// en versiones >= 2.40a el FIFO está en 0x200.
// EL controlador suele reportar VERID=0x290a.
#define SDMMC_DATA_OLD  0x100
#define SDMMC_DATA_NEW  0x200

// CTRL bits.
#define CTRL_RESET       (1U << 0)
#define CTRL_FIFO_RESET  (1U << 1)
#define CTRL_DMA_RESET   (1U << 2)
#define CTRL_INT_ENABLE  (1U << 4)
#define CTRL_DMA_ENABLE  (1U << 5)
#define CTRL_USE_IDMAC   (1U << 25)

// CMD bits.
#define CMD_START        (1U << 31)
#define CMD_USE_HOLD_REG (1U << 29)
#define CMD_UPDATE_CLK   (1U << 21)
#define CMD_INIT         (1U << 15)
#define CMD_PRV_DAT_WAIT (1U << 13)
#define CMD_DAT_WR       (1U << 10)
#define CMD_DAT_EXP      (1U << 9)
#define CMD_RESP_CRC     (1U << 8)
#define CMD_RESP_LONG    (1U << 7)
#define CMD_RESP_EXP     (1U << 6)
#define CMD_INDX(n)      ((n) & 0x1f)

// RINTSTS bits.
#define INT_EBE          (1U << 15)
#define INT_SBE          (1U << 13)
#define INT_HLE          (1U << 12)
#define INT_FRUN         (1U << 11)
#define INT_HTO          (1U << 10)
#define INT_DRTO         (1U << 9)
#define INT_RTO          (1U << 8)
#define INT_DCRC         (1U << 7)
#define INT_RCRC         (1U << 6)
#define INT_RXDR         (1U << 5)
#define INT_TXDR         (1U << 4)
#define INT_DATA_OVER    (1U << 3)
#define INT_CMD_DONE     (1U << 2)
#define INT_RESP_ERR     (1U << 1)
#define INT_CD           (1U << 0)

#define INT_ERRORS       (INT_EBE | INT_SBE | INT_HLE | INT_FRUN | \
                          INT_HTO | INT_DRTO | INT_RTO | INT_DCRC | \
                          INT_RCRC | INT_RESP_ERR)

// STATUS bits
#define STATUS_BUSY      (1U << 9)
#define STATUS_FIFO_COUNT(x) (((x) >> 17) & 0x1fff)

#define SD_TIMEOUT 10000000

//flag debug 
#ifndef DBG_SD
#define DBG_SD 0
#endif

#if DBG_SD
#define SD_DEBUG(...) printf(__VA_ARGS__)
#else
#define SD_DEBUG(...)
#endif

static int sd_ready = 0;
static uint sd_data_offset = SDMMC_DATA_NEW;

static inline uint
sd_readl(uint off)
{
  return *(volatile uint *)(SD_BASE + off);
}

static inline void
sd_writel(uint off, uint val)
{
  *(volatile uint *)(SD_BASE + off) = val;
}

static void
sd_panic_status(char *msg)
{
  uint rintsts = sd_readl(SDMMC_RINTSTS);
  uint status  = sd_readl(SDMMC_STATUS);
  uint cmd     = sd_readl(SDMMC_CMD);

  printf("sd: %s\r\n", msg);
  printf("sd: RINTSTS=0x%x STATUS=0x%x CMD=0x%x RESP0=0x%x\r\n",
         rintsts, status, cmd, sd_readl(SDMMC_RESP0));

  panic("sdcard");
}

static void
sd_wait_reset_clear(uint bits)
{
  for(uint i = 0; i < SD_TIMEOUT; i++){
    if((sd_readl(SDMMC_CTRL) & bits) == 0)
      return;
  }

  sd_panic_status("timeout esperando reset FIFO/DMA");
}

static void
sd_clear_interrupts(void)
{
  // En DW MMC, RINTSTS se limpia escribiendo 1s.
  sd_writel(SDMMC_RINTSTS, 0xffffffff);
}

static void
sd_prepare_pio(void)
{
  uint ctrl = sd_readl(SDMMC_CTRL);

  // U-Boot puede haber dejado DMA/IDMAC activado.
  // Para nuestro driver inicial queremos PIO puro.
  ctrl &= ~CTRL_DMA_ENABLE;
  ctrl &= ~CTRL_USE_IDMAC;
  sd_writel(SDMMC_CTRL, ctrl);

  // Resetear FIFO y DMA interno, pero NO resetear todo el controlador.
  sd_writel(SDMMC_CTRL, sd_readl(SDMMC_CTRL) | CTRL_FIFO_RESET | CTRL_DMA_RESET);
  sd_wait_reset_clear(CTRL_FIFO_RESET | CTRL_DMA_RESET);

  // Sin interrupciones reales; usaremos polling.
  sd_writel(SDMMC_INTMASK, 0x0);

  // Limpiar estado pendiente.
  sd_clear_interrupts();

  // FIFO threshold conservador:
  // TX watermark bajo, RX watermark bajo.
  // Para empezar, RX watermark = 1 palabra.
  sd_writel(SDMMC_FIFOTH, (1 << 16) | 0);
}

static void
sd_wait_cmd_idle(void)
{
  for(uint i = 0; i < SD_TIMEOUT; i++){
    if((sd_readl(SDMMC_CMD) & CMD_START) == 0)
      return;
  }

  sd_panic_status("timeout esperando CMD_START=0");
}

static void
sd_wait_data_idle(void)
{
  for(uint i = 0; i < SD_TIMEOUT; i++){
    uint status = sd_readl(SDMMC_STATUS);

    if((status & STATUS_BUSY) == 0)
      return;
  }

  sd_panic_status("timeout esperando STATUS_BUSY=0");
}

static void
sd_send_cmd(uint cmdidx, uint arg, uint flags)
{
  sd_wait_cmd_idle();

  sd_wait_data_idle();

  sd_clear_interrupts();

  sd_writel(SDMMC_CMDARG, arg);

  uint cmd = CMD_START | CMD_USE_HOLD_REG | CMD_PRV_DAT_WAIT |
             flags | CMD_INDX(cmdidx);

  sd_writel(SDMMC_CMD, cmd);

  for(uint i = 0; i < SD_TIMEOUT; i++){
    uint r = sd_readl(SDMMC_RINTSTS);

    if(r & INT_ERRORS){
      printf("sd_send_cmd: cmd=%u arg=0x%x flags=0x%x\r\n",
             cmdidx, arg, flags);
      sd_panic_status("error en comando SD");
    }

    if(r & INT_CMD_DONE){
      // Limpiamos CMD_DONE.
      sd_writel(SDMMC_RINTSTS, INT_CMD_DONE);
      return;
    }
  }

  printf("sd_send_cmd: timeout cmd=%u arg=0x%x flags=0x%x\r\n",
         cmdidx, arg, flags);
  sd_panic_status("timeout esperando CMD_DONE");
}

static void
sd_read_sector(uint64 lba, uchar *dst)
{
  // De momento asumimos SDHC/SDXC
  // En tarjetas SDHC/SDXC, CMD17 usa número de sector
  // En SDSC antiguas usaría dirección en bytes
  uint arg = (uint)lba;

  sd_prepare_pio();

  // Preparar transferencia de 1 bloque de 512 bytes
  sd_writel(SDMMC_BLKSIZ, SD_SECTOR_SIZE);
  sd_writel(SDMMC_BYTCNT, SD_SECTOR_SIZE);

  sd_clear_interrupts();

  // CMD17 = READ_SINGLE_BLOCK
  sd_send_cmd(17, arg, CMD_RESP_EXP | CMD_RESP_CRC | CMD_DAT_EXP);

  int words = SD_SECTOR_SIZE / 4;
  int copied = 0;
  uint idle = 0;

  while(copied < words){
    uint status = sd_readl(SDMMC_STATUS);
    uint fcnt = STATUS_FIFO_COUNT(status);

    if(fcnt > 0){
      idle = 0;

      while(fcnt > 0 && copied < words){
        uint v = sd_readl(sd_data_offset);

        dst[copied * 4 + 0] = (uchar)(v & 0xff);
        dst[copied * 4 + 1] = (uchar)((v >> 8) & 0xff);
        dst[copied * 4 + 2] = (uchar)((v >> 16) & 0xff);
        dst[copied * 4 + 3] = (uchar)((v >> 24) & 0xff);

        copied++;
        fcnt--;
      }

      continue;
    }

    uint r = sd_readl(SDMMC_RINTSTS);

    if(copied >= words)
      break;

    if(r & INT_ERRORS){
      printf("sd_read_sector: error leyendo lba=%lu copied=%d\r\n",
            lba, copied);
      sd_panic_status("error durante lectura de datos");
    }

    /*
     DATA_OVER no significa necesariamente que el FIFO esté vacío.
     En la traza de debugg vi:
    
       RINTSTS=0x28 = DATA_OVER | RXDR
      copied=115
    
     Es decir, la transferencia terminó, pero todavía quedaban datos
     pendientes en el FIFO, por eso NO hago panic aquí
    */
    if(r & INT_DATA_OVER){
      status = sd_readl(SDMMC_STATUS);
      fcnt = STATUS_FIFO_COUNT(status);

      if(fcnt > 0)
        continue;
    }

    idle++;
    if(idle > SD_TIMEOUT){
      printf("sd_read_sector: timeout drenando FIFO lba=%lu copied=%d r=0x%x status=0x%x\r\n",
            lba, copied, r, status);
      sd_panic_status("timeout drenando FIFO");
    }
  }

  // Esperar fin de transferencia.
  // Si ya hemos copiado los 512 bytes, FRUN no será fatal en esta versión PIO.
  for(uint i = 0; i < SD_TIMEOUT; i++){
    uint r = sd_readl(SDMMC_RINTSTS);

    // Si DATA_OVER está marcado, aceptamos la lectura.
    // En este controlador puede venir junto con FRUN/RXDR.
    if(r & INT_DATA_OVER){
      sd_writel(SDMMC_RINTSTS, INT_DATA_OVER | INT_RXDR | INT_FRUN);

      SD_DEBUG("sd_read_sector: OK lba=%lu first=%x %x %x %x\r\n",
            lba, dst[0], dst[1], dst[2], dst[3]);
      return;
    }

    // Solo consideramos fatales los errores distintos de FRUN.
    uint fatal = r & (INT_ERRORS & ~INT_FRUN);

    if(fatal){
      printf("sd_read_sector: error final fatal lba=%lu r=0x%x\r\n", lba, r);
      sd_panic_status("error final en lectura");
    }
  }

  printf("sd_read_sector: timeout esperando DATA_OVER lba=%lu\r\n", lba);
  sd_panic_status("timeout DATA_OVER");
}

static void
sd_write_sector(uint64 lba, uchar *src)
{
  uint arg = (uint)lba;

  sd_prepare_pio();

  // Preparar transferencia de 1 bloque de 512 bytes.
  sd_writel(SDMMC_BLKSIZ, SD_SECTOR_SIZE);
  sd_writel(SDMMC_BYTCNT, SD_SECTOR_SIZE);

  sd_clear_interrupts();

  // CMD24 = WRITE_SINGLE_BLOCK.
  sd_send_cmd(24, arg,
              CMD_RESP_EXP | CMD_RESP_CRC | CMD_DAT_EXP | CMD_DAT_WR);

  int words = SD_SECTOR_SIZE / 4;
  int written = 0;
  uint idle = 0;

  while(written < words){
    uint status = sd_readl(SDMMC_STATUS);
    uint fcnt = STATUS_FIFO_COUNT(status);

    /*
      El FIFO tiene 32 palabras según HCON/driver Linux típico.
      Dejamos margen: si hay menos de 31 palabras, escribimos.
     */
    if(fcnt < 31){
      idle = 0;

      uint v =
        ((uint)src[written * 4 + 0]) |
        ((uint)src[written * 4 + 1] << 8) |
        ((uint)src[written * 4 + 2] << 16) |
        ((uint)src[written * 4 + 3] << 24);

      sd_writel(sd_data_offset, v);
      written++;
      continue;
    }

    uint r = sd_readl(SDMMC_RINTSTS);
    uint fatal = r & (INT_ERRORS & ~INT_FRUN);

    if(fatal){
      printf("sd_write_sector: error fatal lba=%lu written=%d r=0x%x\r\n",
             lba, written, r);
      sd_panic_status("error durante escritura de datos");
    }

    idle++;
    if(idle > SD_TIMEOUT){
      printf("sd_write_sector: timeout llenando FIFO lba=%lu written=%d r=0x%x status=0x%x\r\n",
             lba, written, r, status);
      sd_panic_status("timeout escribiendo FIFO");
    }
  }

  // Esperar fin de transferencia.
  for(uint i = 0; i < SD_TIMEOUT; i++){
    uint r = sd_readl(SDMMC_RINTSTS);

    uint fatal = r & (INT_ERRORS & ~INT_FRUN);

    if(fatal){
      printf("sd_write_sector: error final fatal lba=%lu r=0x%x\r\n", lba, r);
      sd_panic_status("error final en escritura");
    }

    if(r & INT_DATA_OVER){
      sd_writel(SDMMC_RINTSTS, INT_DATA_OVER | INT_TXDR | INT_FRUN);

      /*
      * IMPORTANTE:
      * DATA_OVER significa que el controlador terminó de transferir datos,
      * pero la tarjeta puede seguir ocupada programando internamente el bloque.
      * Si lanzamos otro CMD24 demasiado pronto, aparece RTO.
      */
      sd_wait_data_idle();

      SD_DEBUG("sd_write_sector: OK lba=%lu first=%x %x %x %x\r\n",
            lba, src[0], src[1], src[2], src[3]);
      return;
    }
  }

  printf("sd_write_sector: timeout esperando DATA_OVER lba=%lu\r\n", lba);
  sd_panic_status("timeout DATA_OVER escritura");
}

void
sd_init(void)
{
  printf("sd_init: microSD driver PIO/CMD17\r\n");

  if(BSIZE != 1024)
    panic("sd_init: este codigo asume BSIZE=1024");

  uint ver  = sd_readl(SDMMC_VERID) & 0xffff;
  uint hcon = sd_readl(SDMMC_HCON);
  uint status = sd_readl(SDMMC_STATUS);
  uint cdetect = sd_readl(SDMMC_CDETECT);

  if(ver >= 0x240a)
    sd_data_offset = SDMMC_DATA_NEW;
  else
    sd_data_offset = SDMMC_DATA_OLD;

  printf("sd: VERID=0x%x HCON=0x%x STATUS=0x%x CDETECT=0x%x DATA=0x%x\r\n",
         ver, hcon, status, cdetect, sd_data_offset);

  sd_prepare_pio();

  // Asegurar tamaño de bloque y timeout amplio.
  sd_writel(SDMMC_BLKSIZ, SD_SECTOR_SIZE);
  sd_writel(SDMMC_BYTCNT, 0);
  sd_writel(SDMMC_TMOUT, 0xffffffff);

  sd_ready = 1;

  printf("sd_init: listo para lectura CMD17 desde LBA %lu\r\n",
         (uint64)XV6_FS_START_LBA);
}

void
sd_rw(struct buf *b, int write)
{
  if(!sd_ready)
    panic("sd_rw: SD no inicializada");

  if(BSIZE % SD_SECTOR_SIZE)
    panic("sd_rw: BSIZE no multiplo de 512");

  uint64 lba = XV6_FS_START_LBA +
               ((uint64)b->blockno * SECTORS_PER_XV6_BLOCK);

  if(write){
    sd_write_sector(lba, b->data);
    sd_write_sector(lba + 1, b->data + SD_SECTOR_SIZE);
  } else {
    sd_read_sector(lba, b->data);
    sd_read_sector(lba + 1, b->data + SD_SECTOR_SIZE);
    b->valid = 1;
  }

  b->disk = 0;
}