/*
xv6_tcc_reloc.h

Aplicacion modular de relocaciones RISC-V.
Conserva el registro que empareja PCREL_HI20 con PCREL_LO12 y permite
corregir branches, jal, call y direcciones generadas por la pseudo la.
*/
#ifndef XV6_TCC_RELOC_H
#define XV6_TCC_RELOC_H

#define XV6_TCC_RELOC_OK 0
#define XV6_TCC_RELOC_RANGE 1
#define XV6_TCC_RELOC_ALIGNMENT 2
#define XV6_TCC_RELOC_BOUNDS 3
#define XV6_TCC_RELOC_UNSUPPORTED 4

struct Xv6TccPcrelHi {
  uint64 address;
  uint64 value;
};

struct Xv6TccPcrelTable {
  struct Xv6TccPcrelHi *entries;
  int count;
  int capacity;
};

int xv6_tcc_apply_relocation(
    uchar *image, uint image_size, uint offset,
    uint type, uint64 place, uint64 value,
    struct Xv6TccPcrelTable *pcrel);

#endif
