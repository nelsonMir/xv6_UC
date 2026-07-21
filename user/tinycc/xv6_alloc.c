#include "kernel/types.h"
#include "user/user.h"

#include "xv6_alloc.h"

/*Guardo delante de cada bloque su tamaño original para poder
implementar realloc sobre el malloc sencillo de xv6*/

typedef union xv6_tcc_block {
  struct {
    unsigned long size;
  } meta;
  uint64 alignment;
} Xv6TccBlock;

#define XV6_TCC_MAX_ALLOC 0x7fffffffUL

static void *
allocate_block(unsigned long size)
{
  Xv6TccBlock *block;
  unsigned long total;

  if(size == 0)
    return 0;

  if(size > XV6_TCC_MAX_ALLOC - sizeof(Xv6TccBlock))
    return 0;

  total = sizeof(Xv6TccBlock) + size;

  block = malloc((uint)total);
  if(block == 0)
    return 0;

  block->meta.size = size;

  return block + 1;
}

void *
xv6_tcc_realloc(void *ptr, unsigned long size)
{
  Xv6TccBlock *old_block;
  void *new_ptr;
  unsigned long copy_size;

  // Libero el bloque cuando el nuevo tamaño es cero
  if(size == 0){
    if(ptr != 0){
      old_block = (Xv6TccBlock *)ptr - 1;
      free(old_block);
    }

    return 0;
  }

  // Creo un bloque nuevo cuando no existe uno anterior
  if(ptr == 0)
    return allocate_block(size);

  old_block = (Xv6TccBlock *)ptr - 1;

  new_ptr = allocate_block(size);
  if(new_ptr == 0)
    return 0;

  copy_size = old_block->meta.size;
  if(copy_size > size)
    copy_size = size;

  /*SE conservan los datos existentes hasta el menor tamaño entre
  el bloque antiguo y el bloque nuevo*/

  if(copy_size != 0)
    memmove(new_ptr, ptr, (int)copy_size);

  free(old_block);

  return new_ptr;
}