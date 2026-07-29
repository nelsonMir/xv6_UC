#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_writer.h"

static struct Xv6TccObjectBuilder object;
static uchar text_data[64];
static uchar symtab_data[1024];
static char strtab_data[512];
static uchar rela_text_data[512];
static char shstrtab_data[256];
static uchar image_data[4096];

static void
fail(const char *test)
{
  fprintf(2, "objwritetest: fallo: %s\n", test);
  exit(1);
}

static void
process(const char *text)
{
  struct Xv6TccParsedLine line;

  if(xv6_tcc_parse_line(text, &line) < 0 ||
     xv6_tcc_object_process_line(&object, &line) < 0){
    fprintf(2, "objwritetest: no se pudo procesar: %s\n", text);
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
  struct Xv6TccElfHeader *header;
  struct Xv6TccElfSectionHeader *sections;
  int file;
  uchar magic[4];

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

  if(xv6_tcc_object_finalize(&object) < 0)
    fail("finalizacion del objeto");

  if(xv6_tcc_build_rel_elf(&object, &image, &shstrtab) < 0)
    fail("construccion ELF");

  header = (struct Xv6TccElfHeader *)image.data;
  if(header->e_ident[0] != 0x7f ||
     header->e_ident[1] != 'E' ||
     header->e_ident[2] != 'L' ||
     header->e_ident[3] != 'F' ||
     header->e_ident[4] != XV6_TCC_ELFCLASS64 ||
     header->e_ident[5] != XV6_TCC_ELFDATA2LSB)
    fail("identificacion ELF");

  if(header->e_type != XV6_TCC_ET_REL ||
     header->e_machine != XV6_TCC_EM_RISCV ||
     header->e_ehsize != sizeof(*header) ||
     header->e_shentsize != sizeof(struct Xv6TccElfSectionHeader) ||
     header->e_shnum != XV6_TCC_REL_SECTION_COUNT ||
     header->e_shstrndx != XV6_TCC_REL_SECTION_SHSTRTAB)
    fail("cabecera ELF");

  if(header->e_shoff +
         header->e_shnum * sizeof(struct Xv6TccElfSectionHeader) !=
     image.size)
    fail("tabla de secciones al final");

  sections = (struct Xv6TccElfSectionHeader *)(
      image.data + header->e_shoff);

  if(sections[XV6_TCC_REL_SECTION_TEXT].sh_type !=
         XV6_TCC_SHT_PROGBITS ||
     sections[XV6_TCC_REL_SECTION_TEXT].sh_size != 16 ||
     sections[XV6_TCC_REL_SECTION_TEXT].sh_addralign != 4)
    fail("seccion .text");

  if(sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_type !=
         XV6_TCC_SHT_RELA ||
     sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_size !=
         2 * sizeof(struct Xv6TccElfRela) ||
     sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_link !=
         XV6_TCC_REL_SECTION_SYMTAB ||
     sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_info !=
         XV6_TCC_REL_SECTION_TEXT)
    fail("seccion .rela.text");

  if(sections[XV6_TCC_REL_SECTION_SYMTAB].sh_type !=
         XV6_TCC_SHT_SYMTAB ||
     sections[XV6_TCC_REL_SECTION_SYMTAB].sh_link !=
         XV6_TCC_REL_SECTION_STRTAB ||
     sections[XV6_TCC_REL_SECTION_SYMTAB].sh_info != 2 ||
     sections[XV6_TCC_REL_SECTION_SYMTAB].sh_entsize !=
         sizeof(struct Xv6TccElfSym))
    fail("seccion .symtab");

  if(strcmp(shstrtab.data +
                sections[XV6_TCC_REL_SECTION_TEXT].sh_name,
            ".text") != 0 ||
     strcmp(shstrtab.data +
                sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_name,
            ".rela.text") != 0 ||
     strcmp(shstrtab.data +
                sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_name,
            ".shstrtab") != 0)
    fail("nombres de seccion");

  if(xv6_tcc_write_elf_file("stage8test.o", &image) < 0)
    fail("escritura del fichero");

  file = open("stage8test.o", O_RDONLY);
  if(file < 0)
    fail("reapertura del fichero");
  if(read(file, magic, sizeof(magic)) != sizeof(magic)){
    close(file);
    fail("lectura del fichero");
  }
  close(file);

  if(magic[0] != 0x7f || magic[1] != 'E' ||
     magic[2] != 'L' || magic[3] != 'F')
    fail("magic persistido");

  fprintf(1, "objeto ELF64 ET_REL verificado\n");
  fprintf(1, "  cabecera ELF: %d bytes\n", (int)sizeof(*header));
  fprintf(1, "  secciones: NULL, .text, .rela.text, .symtab, .strtab, .shstrtab\n");
  fprintf(1, "  .text: %d bytes\n",
          (int)sections[XV6_TCC_REL_SECTION_TEXT].sh_size);
  fprintf(1, "  .rela.text: %d entradas\n",
          (int)(sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_size /
                sizeof(struct Xv6TccElfRela)));
  fprintf(1, "  fichero stage8test.o: %d bytes\n", image.size);
  fprintf(1, "prueba xv6 superada\n");
  exit(0);
}
