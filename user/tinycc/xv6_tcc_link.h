/*
xv6_tcc_link.h

Resolucion modular de simbolos fuertes/debiles y relocaciones.
Corrige .text, .rodata y .data despues del layout combinado.
*/
#ifndef XV6_TCC_LINK_H
#define XV6_TCC_LINK_H

#include "user/tinycc/xv6_tcc_layout.h"
#include "user/tinycc/xv6_tcc_reloc.h"

#define XV6_TCC_LINK_MAX_GLOBALS 128
#define XV6_TCC_LINK_MAX_PCREL 128

#define XV6_TCC_LINK_OK 0
#define XV6_TCC_LINK_INVALID 1
#define XV6_TCC_LINK_TOO_MANY_GLOBALS 2
#define XV6_TCC_LINK_DUPLICATE_SYMBOL 3
#define XV6_TCC_LINK_UNDEFINED_SYMBOL 4
#define XV6_TCC_LINK_BAD_SYMBOL 5
#define XV6_TCC_LINK_BAD_RELOCATION 6
#define XV6_TCC_LINK_RELOCATION_RANGE 7
#define XV6_TCC_LINK_RELOCATION_ALIGNMENT 8
#define XV6_TCC_LINK_RELOCATION_BOUNDS 9
#define XV6_TCC_LINK_RELOCATION_UNSUPPORTED 10

struct Xv6TccGlobalSymbol {
  char name[XV6_TCC_LINE_NAME_MAX];
  int defined;
  int weak_definition;
  int strong_reference;
  int object_index;
  uint symbol_index;
  ushort section_index;
  uint64 value;
};

struct Xv6TccLinkState {
  const struct Xv6TccRelObjectView *views;
  const struct Xv6TccLinkLayout *layout;
  int input_count;

  struct Xv6TccGlobalSymbol globals[XV6_TCC_LINK_MAX_GLOBALS];
  int global_count;
  int applied_relocations;

  struct Xv6TccPcrelHi pcrel_entries[XV6_TCC_LINK_MAX_PCREL];
  struct Xv6TccPcrelTable pcrel_table;

  char error_symbol[XV6_TCC_LINE_NAME_MAX];
  int error_object;
  int error_relocation;
};

int xv6_tcc_link_resolve(
    struct Xv6TccLinkState *state,
    const struct Xv6TccRelObjectView *views,
    int input_count,
    const struct Xv6TccLinkLayout *layout,
    struct Xv6TccElfBuffer *combined_text);

const struct Xv6TccGlobalSymbol *xv6_tcc_link_find_global(
    const struct Xv6TccLinkState *state,
    const char *name);

const char *xv6_tcc_link_status_text(int status);

#endif
