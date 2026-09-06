/*
xv6_tcc_exec_writer.h

Construccion del ejecutable ELF64 RISC-V ET_EXEC.
Recibe las secciones ya combinadas y relocadas, calcula el punto de entrada
y crea un unico segmento PT_LOAD compatible con exec de xv6.
*/
#ifndef XV6_TCC_EXEC_WRITER_H
#define XV6_TCC_EXEC_WRITER_H

#include "user/tinycc/xv6_tcc_link.h"

#define XV6_TCC_EXEC_LOAD_OFFSET 0x1000

struct Xv6TccElfProgramHeader {
  uint p_type;
  uint p_flags;
  uint64 p_offset;
  uint64 p_vaddr;
  uint64 p_paddr;
  uint64 p_filesz;
  uint64 p_memsz;
  uint64 p_align;
};

int xv6_tcc_build_exec_elf(
    const struct Xv6TccLinkLayout *layout,
    const struct Xv6TccLinkState *link_state,
    const struct Xv6TccElfBuffer *text,
    const struct Xv6TccElfBuffer *rodata,
    const struct Xv6TccElfBuffer *data,
    struct Xv6TccElfBuffer *image);

int xv6_tcc_write_exec_file(
    const struct Xv6TccLinkLayout *layout,
    const struct Xv6TccLinkState *link_state,
    const struct Xv6TccElfBuffer *text,
    const struct Xv6TccElfBuffer *rodata,
    const struct Xv6TccElfBuffer *data,
    const char *path,
    struct Xv6TccElfBuffer *image);

#endif
