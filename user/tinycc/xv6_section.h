#ifndef XV6_TCC_SECTION_H
#define XV6_TCC_SECTION_H

#include "elf.h"

typedef struct TCCState TCCState;

/*Se conserva la estructura Section utilizada por TinyCC.
Algunos campos todavía no se utilizarán, pero se mantienen
para facilitar la integración posterior de tccelf.c*/

typedef struct Section {
  unsigned long data_offset;
  unsigned char *data;
  unsigned long data_allocated;

  TCCState *s1;

  int sh_name;
  int sh_num;
  int sh_type;
  int sh_flags;
  int sh_info;
  int sh_addralign;
  int sh_entsize;

  unsigned long sh_size;
  Elf64_Addr sh_addr;
  unsigned long sh_offset;

  int nb_hashed_syms;

  struct Section *link;
  struct Section *reloc;
  struct Section *hash;
  struct Section *prev;

  char name[1];
} Section;

// Se crea una sección vacía con el nombre y atributos indicados
Section *xv6_tcc_new_section(const char *name,
                             int sh_type,
                             int sh_flags);

// Se libera el contenido y la estructura de una sección
void xv6_tcc_delete_section(Section *section);

// Se amplía la memoria reservada para una sección
int xv6_tcc_section_realloc(Section *section,
                            unsigned long new_size);

// Se reserva espacio alineado dentro de una sección
int xv6_tcc_section_add(Section *section,
                        unsigned long size,
                        int align,
                        unsigned long *offset_result);

// Se reserva espacio sin alineamiento adicional
void *xv6_tcc_section_ptr_add(Section *section,
                              unsigned long size);

// Se comprueba el funcionamiento básico del modelo de secciones
int xv6_tcc_check_section_model(void);

#endif