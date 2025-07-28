#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"
#include "defs.h"
#include "fsimg.h"  // contiene fs_img y fs_img_len



struct {
  struct spinlock lock;
} ramdisk;

void
virtio_disk_init(void)
{
  initlock(&ramdisk.lock, "ramdisk");
  printf("ramdisk_xv6: usando fs.img embebido (%d bytes)\r\n", fs_img_len);
}

void
virtio_disk_rw(struct buf *b, int write)
{
  if (b->blockno >= (fs_img_len / BSIZE))
    panic("ramdisk: blockno fuera de rango");

  acquire(&ramdisk.lock);

  printf("ramdisk: %s bloque %d (ptr=%p)\r\n", write ? "escribiendo" : "leyendo", b->blockno, fs_img + b->blockno * BSIZE);


  uchar *disk = fs_img + b->blockno * BSIZE;
  if (write)
    memmove(disk, b->data, BSIZE);
  else
    memmove(b->data, disk, BSIZE);

  b->valid = 1;
  b->disk = 1;

  release(&ramdisk.lock);
}

void
virtio_disk_intr(void)
{
  // No hace nada: sin interrupciones reales.
}
