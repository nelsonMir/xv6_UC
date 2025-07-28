// ramdisk_xv6.c

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"
#include "memlayout.h"
#include "riscv.h"

#include "fsimg.h"  // fs_img[], fs_img_len

struct {
  struct spinlock lock;
} ramdisk;

void
virtio_disk_init(void)
{
  initlock(&ramdisk.lock, "ramdisk");
  printf("ramdisk_xv6: using fs.img from memory (%d bytes)\n", fs_img_len);
}

void
virtio_disk_rw(struct buf *b, int write)
{
  if (b->blockno >= (fs_img_len / BSIZE))
    panic("virtio_disk_rw: blockno out of range");

  acquire(&ramdisk.lock);

  uchar *disk = fs_img + b->blockno * BSIZE;
  if (write) {
    memmove(disk, b->data, BSIZE);
  } else {
    memmove(b->data, disk, BSIZE);
  }

  b->valid = 1;
  b->disk = 1;
  release(&ramdisk.lock);
}
