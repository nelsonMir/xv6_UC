/*
xv6_tcc_reloc.c

Despachador modular de relocaciones RISC-V
La distribucion de bits y las formulas se adaptan selectivamente de
TinyCC riscv64-link.c, commit d9d02c56401e43be43760b63f7d82f771a7ed1f6.


*/

#include "kernel/types.h"
#include "user/tinycc/xv6_tcc_object.h"
#include "user/tinycc/xv6_tcc_reloc.h"

typedef long int64;

static uint
read32le(const uchar *data)
{
  return (uint)data[0] |
         ((uint)data[1] << 8) |
         ((uint)data[2] << 16) |
         ((uint)data[3] << 24);
}

static void
write32le(uchar *data, uint value)
{
  data[0] = value & 0xff;
  data[1] = (value >> 8) & 0xff;
  data[2] = (value >> 16) & 0xff;
  data[3] = (value >> 24) & 0xff;
}

static void
write64le(uchar *data, uint64 value)
{
  int i;

  for(i = 0; i < 8; i++)
    data[i] = (uchar)(value >> (8 * i));
}

static int
bounds_ok(uint size, uint offset, uint bytes)
{
  return offset <= size && bytes <= size - offset;
}

static int
record_hi(struct Xv6TccPcrelTable *table,
          uint64 address, uint64 value)
{
  if(!table || !table->entries || table->count >= table->capacity)
    return XV6_TCC_RELOC_BOUNDS;
  table->entries[table->count].address = address;
  table->entries[table->count].value = value;
  table->count++;
  return XV6_TCC_RELOC_OK;
}

static int
lookup_hi(const struct Xv6TccPcrelTable *table,
          uint64 address, uint64 *value)
{
  int i;

  if(!table || !value)
    return XV6_TCC_RELOC_UNSUPPORTED;
  for(i = table->count - 1; i >= 0; i--){
    if(table->entries[i].address == address){
      *value = table->entries[i].value;
      return XV6_TCC_RELOC_OK;
    }
  }
  return XV6_TCC_RELOC_UNSUPPORTED;
}

int
xv6_tcc_apply_relocation(uchar *image, uint image_size,
                          uint offset, uint type,
                          uint64 place, uint64 value,
                          struct Xv6TccPcrelTable *pcrel)
{
  uchar *destination;
  uint64 displacement64;
  uint64 high_value;
  uint displacement32;
  uint instruction;
  int status;

  if(!image)
    return XV6_TCC_RELOC_BOUNDS;

  if(type == XV6_TCC_R_RISCV_32){
    if(!bounds_ok(image_size, offset, 4))
      return XV6_TCC_RELOC_BOUNDS;
    if(value > 0xffffffffULL)
      return XV6_TCC_RELOC_RANGE;
    write32le(image + offset, (uint)value);
    return XV6_TCC_RELOC_OK;
  }

  if(type == XV6_TCC_R_RISCV_64){
    if(!bounds_ok(image_size, offset, 8))
      return XV6_TCC_RELOC_BOUNDS;
    write64le(image + offset, value);
    return XV6_TCC_RELOC_OK;
  }

  if(type == XV6_TCC_R_RISCV_BRANCH){
    if(!bounds_ok(image_size, offset, 4))
      return XV6_TCC_RELOC_BOUNDS;
    displacement64 = value - place;
    if((displacement64 + (1U << 12)) & ~(uint64)0x1ffe)
      return XV6_TCC_RELOC_RANGE;
    if(displacement64 & 1)
      return XV6_TCC_RELOC_ALIGNMENT;

    displacement32 = (uint)(displacement64 >> 1);
    destination = image + offset;
    instruction = read32le(destination);
    write32le(destination,
      (instruction & ~0xfe000f80U) |
      ((displacement32 & 0x800U) << 20) |
      ((displacement32 & 0x3f0U) << 21) |
      ((displacement32 & 0x00fU) << 8) |
      ((displacement32 & 0x400U) >> 3));
    return XV6_TCC_RELOC_OK;
  }

  if(type == XV6_TCC_R_RISCV_JAL){
    if(!bounds_ok(image_size, offset, 4))
      return XV6_TCC_RELOC_BOUNDS;
    displacement64 = value - place;
    if((displacement64 + (1U << 21)) &
       ~(((uint64)1 << 22) - 2))
      return XV6_TCC_RELOC_RANGE;
    if(displacement64 & 1)
      return XV6_TCC_RELOC_ALIGNMENT;

    displacement32 = (uint)displacement64;
    destination = image + offset;
    instruction = read32le(destination);
    write32le(destination,
      (instruction & 0xfffU) |
      (((displacement32 >> 12) & 0xffU) << 12) |
      (((displacement32 >> 11) & 1U) << 20) |
      (((displacement32 >> 1) & 0x3ffU) << 21) |
      (((displacement32 >> 20) & 1U) << 31));
    return XV6_TCC_RELOC_OK;
  }

  if(type == XV6_TCC_R_RISCV_CALL){
    if(!bounds_ok(image_size, offset, 8))
      return XV6_TCC_RELOC_BOUNDS;
    displacement64 = value - place;
    if((int64)displacement64 < -((int64)1 << 31) ||
       (int64)displacement64 > (((int64)1 << 31) - 1))
      return XV6_TCC_RELOC_RANGE;

    destination = image + offset;
    write32le(destination,
      (read32le(destination) & 0xfffU) |
      (((uint)displacement64 + 0x800U) & ~0xfffU));
    write32le(destination + 4,
      (read32le(destination + 4) & 0xfffffU) |
      (((uint)displacement64 & 0xfffU) << 20));
    return XV6_TCC_RELOC_OK;
  }

  if(type == XV6_TCC_R_RISCV_PCREL_HI20){
    if(!bounds_ok(image_size, offset, 4))
      return XV6_TCC_RELOC_BOUNDS;
    displacement64 = (uint64)((int64)(value - place + 0x800) >> 12);
    if((displacement64 + ((uint64)1 << 20)) >> 21)
      return XV6_TCC_RELOC_RANGE;

    destination = image + offset;
    write32le(destination,
      (read32le(destination) & 0xfffU) |
      (((uint)displacement64 & 0xfffffU) << 12));
    return record_hi(pcrel, place, value);
  }

  if(type == XV6_TCC_R_RISCV_PCREL_LO12_I){
    if(!bounds_ok(image_size, offset, 4))
      return XV6_TCC_RELOC_BOUNDS;
    status = lookup_hi(pcrel, value, &high_value);
    if(status != XV6_TCC_RELOC_OK)
      return status;

    destination = image + offset;
    write32le(destination,
      (read32le(destination) & 0xfffffU) |
      (((uint)(high_value - value) & 0xfffU) << 20));
    return XV6_TCC_RELOC_OK;
  }

  return XV6_TCC_RELOC_UNSUPPORTED;
}
