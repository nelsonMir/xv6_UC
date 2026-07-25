/*
  xv6_tcc_elf.h
 
  Secciones y helpers ELF reducidos a partir de tccelf.c de TinyCC.
  FUnción original: https://raw.githubusercontent.com/TinyCC/tinycc/d9d02c56401e43be43760b63f7d82f771a7ed1f6/tccelf.c
 */
#ifndef XV6_TCC_ELF_H
#define XV6_TCC_ELF_H

/*ALmacena bloques de datos de una sección:
  - data: Es la dirección de la memoria reservada donde se guardarán los elementos de la sección
  - size: número de bytes que ya  pertenecen a la sección (se va llenando porque voy metiendo elementos )
  - capactiy: número de bytes que caben en la sección
  
TInyCC usa arrays dinámicos, es decir, va amplicando la memoria automáticamente. COmo en esta versión de xv6 
aún no he creado la syscall realloc se va a trabajar con búferes de capacidad fija*/
struct Xv6TccElfBuffer {
  uchar *data;
  uint size;
  uint capacity;
};

struct Xv6TccElfStringTable {
  char *data;
  uint size;
  uint capacity;
};

/*Representa una entrada ELF64 de la tabla de símbolos*/
struct Xv6TccElfSym {
  uint st_name;
  uchar st_info;
  uchar st_other;
  ushort st_shndx;
  uint64 st_value;
  uint64 st_size;
};

//Reprenta un relocación ELF64 con addend explícito 
struct Xv6TccElfRela {
  uint64 r_offset;
  uint64 r_info;
  long r_addend;
};

int xv6_tcc_section_add(struct Xv6TccElfBuffer *section,
                        uint bytes, uint align, uint *offset);
int xv6_tcc_put_elf_str(struct Xv6TccElfStringTable *table,
                        const char *text, uint *offset);
int xv6_tcc_put_elf_sym_raw(struct Xv6TccElfBuffer *symtab,
                            uint name_offset, uint64 value, uint64 size,
                            int info, int other, int shndx, uint *index);
int xv6_tcc_put_elf_sym(struct Xv6TccElfBuffer *symtab,
                        struct Xv6TccElfStringTable *strtab,
                        uint64 value, uint64 size, int info, int other,
                        int shndx, const char *name, uint *index);
int xv6_tcc_put_elf_rela(struct Xv6TccElfBuffer *rela,
                         uint64 offset, uint64 info, long addend);

#endif
