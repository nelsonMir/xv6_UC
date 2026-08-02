#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_reader.h"

static struct Xv6TccObjectBuilder object;
static uchar text_data[64];
static uchar symtab_data[1024];
static char strtab_data[512];
static uchar rela_text_data[512];
static char shstrtab_data[256];
static uchar image_data[4096];
static uchar file_data[4096];
static uchar corrupt_data[4096];

static void
fail(const char *test)
{
  fprintf(2, "elfreadtest: fallo: %s\n", test);
  exit(1);
}

static void
process(const char *text)
{
  struct Xv6TccParsedLine line;

  if(xv6_tcc_parse_line(text, &line) < 0 ||
     xv6_tcc_object_process_line(&object, &line) < 0){
    fprintf(2, "elfreadtest: no se pudo procesar: %s\n", text);
    exit(1);
  }
}

int
main(void)
{
  struct Xv6TccElfBuffer text;
  struct Xv6TccElfBuffer symtab;
  struct Xv6TccElfStringTable strtab;
  struct Xv6TccElfBuffer rela_text;
  struct Xv6TccElfStringTable shstrtab;
  struct Xv6TccElfBuffer image;
  struct Xv6TccElfBuffer storage;
  struct Xv6TccRelObjectView view;
  struct Xv6TccRelObjectView corrupt_view;
  const struct Xv6TccElfSym *symbol;
  const struct Xv6TccElfRela *relocation;

  text.data = text_data;
  text.capacity = sizeof(text_data);
  symtab.data = symtab_data;
  symtab.capacity = sizeof(symtab_data);
  strtab.data = strtab_data;
  strtab.capacity = sizeof(strtab_data);
  rela_text.data = rela_text_data;
  rela_text.capacity = sizeof(rela_text_data);
  shstrtab.data = shstrtab_data;
  shstrtab.capacity = sizeof(shstrtab_data);
  image.data = image_data;
  image.capacity = sizeof(image_data);
  storage.data = file_data;
  storage.capacity = sizeof(file_data);

  if(xv6_tcc_object_init(&object, &text, &symtab,
                         &strtab, &rela_text) < 0)
    fail("inicializacion");

  process(".text");
  process(".globl main");
  process("main:");
  process("addi a0, zero, 1");
  process("beq a0, zero, done");
  process("j external_func");
  process("done:");
  process("ret");

  if(xv6_tcc_object_finalize(&object) < 0 ||
     xv6_tcc_build_rel_elf(&object, &image, &shstrtab) < 0 ||
     xv6_tcc_write_elf_file("stage9test.o", &image) < 0)
    fail("creacion del objeto de prueba");

  if(xv6_tcc_load_rel_object("stage9test.o", &storage, &view) < 0)
    fail("lectura del objeto");

  if(view.header->e_type != XV6_TCC_ET_REL ||
     view.header->e_machine != XV6_TCC_EM_RISCV ||
     view.header->e_shnum != 6 ||
     view.text_section->sh_size != 16 ||
     view.symbol_count != 4 ||
     view.relocation_count != 2)
    fail("resumen del objeto");

  if(strcmp(xv6_tcc_rel_section_name(&view, view.text_index),
            ".text") != 0 ||
     strcmp(xv6_tcc_rel_section_name(&view, view.rela_text_index),
            ".rela.text") != 0 ||
     xv6_tcc_rel_find_section(&view, ".symtab") !=
         view.symtab_index)
    fail("nombres y busqueda de secciones");

  symbol = xv6_tcc_rel_symbol_at(&view, 1);
  if(!symbol || strcmp(xv6_tcc_rel_symbol_name(&view, symbol),
                       "done") != 0 ||
     symbol->st_value != 12 || symbol->st_shndx != view.text_index)
    fail("simbolo local done");

  symbol = xv6_tcc_rel_symbol_at(&view, 2);
  if(!symbol || strcmp(xv6_tcc_rel_symbol_name(&view, symbol),
                       "main") != 0 ||
     symbol->st_value != 0 || symbol->st_shndx != view.text_index)
    fail("simbolo global main");

  relocation = xv6_tcc_rel_relocation_at(&view, 0);
  if(!relocation || relocation->r_offset != 4 ||
     xv6_tcc_elf_r_symbol(relocation->r_info) != 1 ||
     xv6_tcc_elf_r_type(relocation->r_info) !=
         XV6_TCC_R_RISCV_BRANCH)
    fail("relocacion branch");

  relocation = xv6_tcc_rel_relocation_at(&view, 1);
  if(!relocation || relocation->r_offset != 8 ||
     xv6_tcc_elf_r_symbol(relocation->r_info) != 3 ||
     xv6_tcc_elf_r_type(relocation->r_info) !=
         XV6_TCC_R_RISCV_JAL)
    fail("relocacion jal");

  memmove(corrupt_data, image.data, image.size);
  corrupt_data[0] = 0;
  if(xv6_tcc_parse_rel_object(corrupt_data, image.size,
                              &corrupt_view) == 0)
    fail("rechazo de magic ELF invalido");

  fprintf(1, "lector ELF64 ET_REL verificado\n");
  fprintf(1, "  fichero: stage9test.o, %d bytes\n", storage.size);
  fprintf(1, "  secciones validadas: %d\n", view.header->e_shnum);
  fprintf(1, "  .text: %d bytes\n", (int)view.text_section->sh_size);
  fprintf(1, "  .symtab: %d entradas\n", view.symbol_count);
  fprintf(1, "  .rela.text: %d entradas\n", view.relocation_count);
  fprintf(1, "  objeto corrupto rechazado correctamente\n");
  fprintf(1, "prueba xv6 superada\n");
  exit(0);
}
