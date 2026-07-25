#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf.h"

static void
fail(const char *test)
{
  fprintf(2, "elftest: fallo: %s\n", test);
  exit(1);
}

int
main(void)
{
  uchar section_data[32];
  char string_data[64];
  uint64 symbol_storage[8];
  uint64 rela_storage[8];
  struct Xv6TccElfBuffer section;
  struct Xv6TccElfStringTable strtab;
  struct Xv6TccElfBuffer symtab;
  struct Xv6TccElfBuffer rela;
  struct Xv6TccElfSym *symbol;
  struct Xv6TccElfRela *relocation;
  uint offset;
  uint main_name;
  uint message_name;
  uint symbol_index;

  memset(section_data, 0xaa, sizeof(section_data));
  section.data = section_data;
  section.size = 0;
  section.capacity = sizeof(section_data);

  if(xv6_tcc_section_add(&section, 3, 1, &offset) < 0 ||
     offset != 0 || section.size != 3)
    fail("primera reserva de seccion");

  section.data[0] = 0x13;
  section.data[1] = 0x05;
  section.data[2] = 0xa0;

  if(xv6_tcc_section_add(&section, 4, 4, &offset) < 0 ||
     offset != 4 || section.size != 8)
    fail("reserva alineada a 4");

  if(section.data[3] != 0)
    fail("relleno de alineacion a cero");

  if(xv6_tcc_section_add(&section, 4, 8, &offset) < 0 ||
     offset != 8 || section.size != 12)
    fail("reserva alineada a 8");

  if(xv6_tcc_section_add(&section, 1, 3, &offset) == 0)
    fail("rechazo de alineacion no potencia de dos");

  if(xv6_tcc_section_add(&section, 64, 1, &offset) == 0)
    fail("rechazo de desbordamiento");

  memset(string_data, 0, sizeof(string_data));
  strtab.data = string_data;
  strtab.size = 1;
  strtab.capacity = sizeof(string_data);

  if(xv6_tcc_put_elf_str(&strtab, "main", &main_name) < 0 ||
     main_name != 1)
    fail("insercion de main en strtab");

  if(xv6_tcc_put_elf_str(&strtab, "message", &message_name) < 0 ||
     message_name != 6 || strtab.size != 14)
    fail("insercion de message en strtab");

  memset(symbol_storage, 0, sizeof(symbol_storage));
  symtab.data = (uchar *)symbol_storage;
  symtab.size = 0;
  symtab.capacity = sizeof(symbol_storage);

  if(xv6_tcc_put_elf_sym_raw(&symtab, 0, 0, 0,
                             0, 0, 0, &symbol_index) < 0 ||
     symbol_index != 0)
    fail("simbolo nulo");

  if(xv6_tcc_put_elf_sym_raw(&symtab, main_name, 8, 4,
                             0x12, 0, 1, &symbol_index) < 0 ||
     symbol_index != 1)
    fail("simbolo main");

  symbol = &((struct Xv6TccElfSym *)symtab.data)[1];
  if(symbol->st_name != main_name || symbol->st_value != 8 ||
     symbol->st_size != 4 || symbol->st_info != 0x12 ||
     symbol->st_shndx != 1)
    fail("contenido del simbolo main");

  memset(rela_storage, 0, sizeof(rela_storage));
  rela.data = (uchar *)rela_storage;
  rela.size = 0;
  rela.capacity = sizeof(rela_storage);

  if(xv6_tcc_put_elf_rela(&rela, 12,
                          ((uint64)symbol_index << 32) | 18,
                          -4) < 0)
    fail("entrada Rela");

  relocation = (struct Xv6TccElfRela *)rela.data;
  if(relocation->r_offset != 12 || relocation->r_addend != -4)
    fail("contenido de Rela");

  fprintf(1, "[etapa 2] buffers ELF verificados\n");
  fprintf(1, "  seccion: size=%d capacity=%d\n",
          section.size, section.capacity);
  fprintf(1, "  strtab: main=%d message=%d size=%d\n",
          main_name, message_name, strtab.size);
  fprintf(1, "  symtab: simbolos=%ld bytes=%d\n",
          symtab.size / sizeof(struct Xv6TccElfSym), symtab.size);
  fprintf(1, "  rela: entradas=%ld bytes=%d\n",
          rela.size / sizeof(struct Xv6TccElfRela), rela.size);
  fprintf(1, "Etapa 2: prueba xv6 superada\n");
  exit(0);
}
