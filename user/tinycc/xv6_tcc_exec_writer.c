/*
xv6_tcc_exec_writer.c

Escritor modular del ELF64 RISC-V ET_EXEC.
La salida usa un unico PT_LOAD con offset de fichero 0x1000 y direccion
virtual 0, disposicion compatible con el cargador exec de xv6.

La organizacion ELF se inspira en TinyCC tccelf.c, reducida a un ejecutable
estatico sin enlazado dinamico, GOT, PLT ni interprete.

*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_writer.h"
#include "user/tinycc/xv6_tcc_exec_writer.h"

#define XV6_TCC_ET_EXEC 2
#define XV6_TCC_PT_LOAD 1
#define XV6_TCC_PF_X 1
#define XV6_TCC_PF_W 2
#define XV6_TCC_PF_R 4

static int
copy_at(struct Xv6TccElfBuffer *image, uint64 offset,
        const struct Xv6TccElfBuffer *source)
{
  if(!image || !source || !image->data || !source->data ||
     offset > image->capacity ||
     source->size > image->capacity - (uint)offset)
    return -1;
  if(source->size)
    memmove(image->data + (uint)offset, source->data, source->size);
  return 0;
}

static void
fill_ident(struct Xv6TccElfHeader *header)
{
  header->e_ident[0] = 0x7f;
  header->e_ident[1] = 'E';
  header->e_ident[2] = 'L';
  header->e_ident[3] = 'F';
  header->e_ident[4] = XV6_TCC_ELFCLASS64;
  header->e_ident[5] = XV6_TCC_ELFDATA2LSB;
  header->e_ident[6] = XV6_TCC_EV_CURRENT;
}

int
xv6_tcc_build_exec_elf(const struct Xv6TccLinkLayout *layout,
                        const struct Xv6TccLinkState *link_state,
                        const struct Xv6TccElfBuffer *text,
                        const struct Xv6TccElfBuffer *rodata,
                        const struct Xv6TccElfBuffer *data,
                        struct Xv6TccElfBuffer *image)
{
  const struct Xv6TccGlobalSymbol *entry;
  struct Xv6TccElfHeader *header;
  struct Xv6TccElfProgramHeader *program;
  uint64 total_size;

  if(!layout || !layout->finalized || !link_state ||
     !text || !rodata || !data || !image || !image->data ||
     sizeof(struct Xv6TccElfHeader) != 64 ||
     sizeof(struct Xv6TccElfProgramHeader) != 56)
    return -1;

  entry = xv6_tcc_link_find_global(link_state, "_start");
  if(!entry || !entry->defined || entry->section_index != XV6_TCC_SHN_TEXT)
    return -1;

  total_size = XV6_TCC_EXEC_LOAD_OFFSET + layout->file_size;
  if(total_size > image->capacity || total_size > 0xffffffffULL)
    return -1;

  memset(image->data, 0, (uint)total_size);
  image->size = (uint)total_size;
  header = (struct Xv6TccElfHeader *)image->data;
  program = (struct Xv6TccElfProgramHeader *)(
      image->data + sizeof(struct Xv6TccElfHeader));

  fill_ident(header);
  header->e_type = XV6_TCC_ET_EXEC;
  header->e_machine = XV6_TCC_EM_RISCV;
  header->e_version = XV6_TCC_EV_CURRENT;
  header->e_entry = entry->value;
  header->e_phoff = sizeof(*header);
  header->e_ehsize = sizeof(*header);
  header->e_phentsize = sizeof(*program);
  header->e_phnum = 1;

  program->p_type = XV6_TCC_PT_LOAD;
  program->p_flags = XV6_TCC_PF_R | XV6_TCC_PF_W | XV6_TCC_PF_X;
  program->p_offset = XV6_TCC_EXEC_LOAD_OFFSET;
  program->p_vaddr = 0;
  program->p_paddr = 0;
  program->p_filesz = layout->file_size;
  program->p_memsz = layout->memory_size;
  program->p_align = 4096;

  if(copy_at(image, XV6_TCC_EXEC_LOAD_OFFSET + layout->text_address,
             text) < 0 ||
     copy_at(image, XV6_TCC_EXEC_LOAD_OFFSET + layout->rodata_address,
             rodata) < 0 ||
     copy_at(image, XV6_TCC_EXEC_LOAD_OFFSET + layout->data_address,
             data) < 0)
    return -1;
  return 0;
}

int
xv6_tcc_write_exec_file(const struct Xv6TccLinkLayout *layout,
                         const struct Xv6TccLinkState *link_state,
                         const struct Xv6TccElfBuffer *text,
                         const struct Xv6TccElfBuffer *rodata,
                         const struct Xv6TccElfBuffer *data,
                         const char *path,
                         struct Xv6TccElfBuffer *image)
{
  if(!path || xv6_tcc_build_exec_elf(layout, link_state,
                                     text, rodata, data, image) < 0)
    return -1;
  return xv6_tcc_write_elf_file(path, image);
}
