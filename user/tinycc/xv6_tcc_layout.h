/*
xv6_tcc_layout.h

Colocacion y combinacion educativa de secciones
Calcula el offset base que recibe cada objeto dentro de las secciones finales.
No resuelve simbolos ni aplica relocaciones
*/
#ifndef XV6_TCC_LAYOUT_H
#define XV6_TCC_LAYOUT_H

#include "user/tinycc/xv6_tcc_elf_reader.h"

#define XV6_TCC_LAYOUT_MAX_INPUTS 8

struct Xv6TccObjectPlacement {
  uint64 text_base;
  uint64 rodata_base;
  uint64 data_base;
  uint64 bss_base;
};

struct Xv6TccLinkLayout {
  struct Xv6TccElfBuffer *text;
  struct Xv6TccElfBuffer *rodata;
  struct Xv6TccElfBuffer *data;
  uint64 bss_size;

  uint text_align;
  uint rodata_align;
  uint data_align;
  uint bss_align;

  uint64 text_address;
  uint64 rodata_address;
  uint64 data_address;
  uint64 bss_address;
  uint64 file_size;
  uint64 memory_size;
  int finalized;

  struct Xv6TccObjectPlacement placements[XV6_TCC_LAYOUT_MAX_INPUTS];
  int input_count;
};

int xv6_tcc_layout_init(struct Xv6TccLinkLayout *layout,
                         struct Xv6TccElfBuffer *text,
                         struct Xv6TccElfBuffer *rodata,
                         struct Xv6TccElfBuffer *data);

int xv6_tcc_layout_finalize(struct Xv6TccLinkLayout *layout);

int xv6_tcc_layout_add_object(
    struct Xv6TccLinkLayout *layout,
    const struct Xv6TccRelObjectView *view);

const struct Xv6TccObjectPlacement *xv6_tcc_layout_placement_at(
    const struct Xv6TccLinkLayout *layout,
    int index);

#endif
