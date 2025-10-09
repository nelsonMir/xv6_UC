#include "types.h"
#include "param.h"
#include "riscv.h"        // (opcional pero recomendable antes de defs.h)
#include "spinlock.h"
#include "sleeplock.h"    // <-- antes de buf.h (para struct sleeplock)
#include "fs.h"           // <-- antes de buf.h (para BSIZE)
#include "buf.h"
#include "defs.h"

extern unsigned char fs_img[];
extern unsigned int  fs_img_len;


struct {
  struct spinlock lock;
} rd;

void
virtio_disk_init(void)
{
  initlock(&rd.lock, "ramdisk");
  printf("ramdisk_xv6: usando fs.img embebido (%u bytes)\r\n", fs_img_len);
}

// acceso a bloque "blockno" (en unidades de BSIZE)
static inline uchar *
blkptr(uint blockno)
{
  return fs_img + (uint64)blockno * BSIZE;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  // Límite de bloques válido según el tamaño de la imagen
  uint maxblocks = fs_img_len / BSIZE;
  if (b->blockno >= maxblocks) {
    panic("ramdisk: blockno fuera de rango");
  }

  acquire(&rd.lock);

  uchar *dst = fs_img + (uint64)b->blockno * BSIZE;

  if (write) {
    // (opcional) depurar escrituras tempranas
    // if (b->blockno < 8) printf("ramdisk: write blk %u\n", b->blockno);
    if (b->blockno < 16) {
      #if DBG_RAMDISK
      printf("ramdisk: write blk %u\r\n", b->blockno);
      #endif
    }
    // Escribir del buffer del kernel al “disco” (RAM)
    memmove(dst, b->data, BSIZE);
    // B_DIRTY lo gestiona log/bwrite; aquí no marcamos nada más.
  } else {
    // Depurar solo los primeros bloques para no inundar la consola
     if (b->blockno < 16) {
      #if DBG_RAMDISK
      printf("ramdisk: read blk %u\r\n", b->blockno);
      #endif
    }

    // Leer del “disco” al buffer
    memmove(b->data, dst, BSIZE);
    b->valid = 1;   // MUY IMPORTANTE: así bread() no esperará nada más
  }

  release(&rd.lock);
}


// Nuestro ramdisk no usa interrupciones. Stub para satisfacer devintr().
void virtio_disk_intr(void) { /* no-op */ }