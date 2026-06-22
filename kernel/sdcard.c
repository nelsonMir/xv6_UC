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

static int sd_ready = 0;

static void
sd_read_sector(uint64 lba, uchar *dst)
{
  printf("sd_read_sector: lba=%lu dst=%p\r\n", lba, dst);
  panic("sd_read_sector: driver SD real todavía no implementado");
}

static void
sd_write_sector(uint64 lba, uchar *src)
{
  printf("sd_write_sector: lba=%lu src=%p\r\n", lba, src);
  panic("sd_write_sector: driver SD real todavía no implementado");
}

void
sd_init(void)
{
  printf("sd_init: microSD driver skeleton\r\n");

  if(BSIZE != 1024)
    panic("sd_init: este codigo asume BSIZE=1024");

  sd_ready = 1;

  printf("sd_init: listo; falta implementar controlador MMC real\r\n");
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