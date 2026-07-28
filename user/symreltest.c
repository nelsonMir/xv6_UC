#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_object.h"

static struct Xv6TccObjectBuilder object;
static uchar text_data[64];
static uchar symtab_data[1024];
static char strtab_data[512];
static uchar rela_data[512];

static void
fail(const char *test)
{
  fprintf(2, "symreltest: fallo: %s\n", test);
  exit(1);
}

static void
process(const char *text)
{
  struct Xv6TccParsedLine line;

  if(xv6_tcc_parse_line(text, &line) < 0 ||
     xv6_tcc_object_process_line(&object, &line) < 0){
    fprintf(2, "symreltest: no se pudo procesar: %s\n", text);
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
  const struct Xv6TccAssemblerSymbol *main_symbol;
  const struct Xv6TccAssemblerSymbol *done_symbol;
  const struct Xv6TccAssemblerSymbol *external_symbol;
  struct Xv6TccElfRela *relocations;

  text.data = text_data;
  text.capacity = sizeof(text_data);
  symtab.data = symtab_data;
  symtab.capacity = sizeof(symtab_data);
  strtab.data = strtab_data;
  strtab.capacity = sizeof(strtab_data);
  rela_text.data = rela_data;
  rela_text.capacity = sizeof(rela_data);

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

  if(object.text->size != 16 || object.symbol_count != 3 ||
     object.relocation_count != 2)
    fail("estado antes de finalizar");

  if(xv6_tcc_object_finalize(&object) < 0)
    fail("finalizacion");

  main_symbol = xv6_tcc_object_find_symbol(&object, "main");
  done_symbol = xv6_tcc_object_find_symbol(&object, "done");
  external_symbol = xv6_tcc_object_find_symbol(&object, "external_func");

  if(!main_symbol || !done_symbol || !external_symbol)
    fail("busqueda de simbolos");

  if(!main_symbol->defined || main_symbol->value != 0 ||
     main_symbol->binding != XV6_TCC_STB_GLOBAL ||
     main_symbol->elf_index != 2)
    fail("simbolo main");

  if(!done_symbol->defined || done_symbol->value != 12 ||
     done_symbol->binding != XV6_TCC_STB_LOCAL ||
     done_symbol->elf_index != 1)
    fail("simbolo done");

  if(external_symbol->defined ||
     external_symbol->section_index != XV6_TCC_SHN_UNDEF ||
     external_symbol->binding != XV6_TCC_STB_GLOBAL ||
     external_symbol->elf_index != 3)
    fail("simbolo externo");

  if(object.first_global_symbol != 2 ||
     symtab.size != 4 * sizeof(struct Xv6TccElfSym) ||
     rela_text.size != 2 * sizeof(struct Xv6TccElfRela))
    fail("tablas ELF finales");

  relocations = (struct Xv6TccElfRela *)rela_text.data;
  if(relocations[0].r_offset != 4 ||
     xv6_tcc_elf_r_symbol(relocations[0].r_info) != done_symbol->elf_index ||
     xv6_tcc_elf_r_type(relocations[0].r_info) != XV6_TCC_R_RISCV_BRANCH ||
     relocations[0].r_addend != 0)
    fail("relocacion branch");

  if(relocations[1].r_offset != 8 ||
     xv6_tcc_elf_r_symbol(relocations[1].r_info) != external_symbol->elf_index ||
     xv6_tcc_elf_r_type(relocations[1].r_info) != XV6_TCC_R_RISCV_JAL ||
     relocations[1].r_addend != 0)
    fail("relocacion jal");

  if(text.data[8] != 0x6f || text.data[9] != 0x00 ||
     text.data[10] != 0x00 || text.data[11] != 0x00 ||
     text.data[12] != 0x67 || text.data[13] != 0x80 ||
     text.data[14] != 0x00 || text.data[15] != 0x00)
    fail("placeholders y ret");

  fprintf(1, "simbolos y relocaciones verificados\n");
  fprintf(1, "  main: global definido en .text+%d, indice ELF %d\n",
          (int)main_symbol->value, main_symbol->elf_index);
  fprintf(1, "  done: local definido en .text+%d, indice ELF %d\n",
          (int)done_symbol->value, done_symbol->elf_index);
  fprintf(1, "  external_func: global indefinido, indice ELF %d\n",
          external_symbol->elf_index);
  fprintf(1, "  .text: %d bytes\n", text.size);
  fprintf(1, "  .symtab: %d simbolos, primer global=%d\n",
          symtab.size / (int)sizeof(struct Xv6TccElfSym),
          object.first_global_symbol);
  fprintf(1, "  .rela.text: %d entradas BRANCH/JAL\n",
          rela_text.size / (int)sizeof(struct Xv6TccElfRela));
  fprintf(1, "prueba xv6 superada\n");
  exit(0);
}
