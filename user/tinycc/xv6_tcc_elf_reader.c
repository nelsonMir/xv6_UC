/*
xv6_tcc_elf_reader.c

Lector educativo de objetos ELF64 RISC-V ET_REL.
Valida la cabecera, la tabla de secciones, las cadenas, los simbolos y las
relocaciones producidas por asxv6. La organizacion se inspira en la lectura
de objetos de TinyCC tccelf.c

Donante conceptual:
  TinyCC tccelf.c
*/



#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_reader.h"

/*Valida que al  leer una región del ELF no se salga del fichero, ósea
que la región esté dentro del fichero:
la región --> [offset, offset + bytes)
donde: 
- offset es la posicón inicial donde se va a leer
- bytes: num bytes a leer del ELF*/
static int
range_valid(uint file_size, uint64 offset, uint64 bytes)
{
  return offset <= file_size && bytes <= (uint64)file_size - offset;
}

static int
power_of_two(uint64 value)
{
  return value != 0 && (value & (value - 1)) == 0;
}

/*Esta función recibe la tabla de cadenas y un offset y devuelve 
la cadena que se encuentra en esa posición*/
static const char *
string_at(const char *table, uint table_size, uint offset)
{
  uint i;

  //compruebo que el offset esté dentro de la tabla de cadenas
  if(!table || offset >= table_size)
    return 0;
  /*Este bucle sirve inicialmente para posicionarnos al inicio de la cadena. EJ:
  m a i n \0
  ^
  |
table[i]
  
  Se va a recorrer la palabra caracter a caracter hasta encontrar el nulo, porque si  no 
  se encuentra el nulo, la cadena está mal*/
  for(i = offset; i < table_size; i++)
    if(table[i] == 0)
      return table + offset;
  return 0;
}

static int
section_data_valid(const struct Xv6TccRelObjectView *view,
                   const struct Xv6TccElfSectionHeader *section)
{
  if(section->sh_type == XV6_TCC_SHT_NULL)
    return section->sh_offset == 0 && section->sh_size == 0 ? 0 : -1;
  if(!power_of_two(section->sh_addralign))
    return -1;
  if(section->sh_type == XV6_TCC_SHT_NOBITS)
    return section->sh_offset <= view->size ? 0 : -1;
  return range_valid(view->size, section->sh_offset,
                     section->sh_size) ? 0 : -1;
}

/*Devuelve el nombre de una sección en la tabla de nombres de secciones "shstrtab" dando su índice*/
const char *
xv6_tcc_rel_section_name(const struct Xv6TccRelObjectView *view,
                          int section_index)
{
  if(!view || !view->header || !view->sections || !view->shstrtab ||
     section_index < 0 || section_index >= view->header->e_shnum)
    return 0;
  //devuelve el nombre
  return string_at(view->shstrtab, view->shstrtab_size,
                   view->sections[section_index].sh_name);
}

/*Devuelve el índice el índice de la cabecera de una sección dado su nombre*/
int
xv6_tcc_rel_find_section(const struct Xv6TccRelObjectView *view,
                          const char *name)
{
  int i;

  if(!view || !view->header || !name)
    return XV6_TCC_READER_SECTION_NOT_FOUND;
   //se recorren todas las secciones y vemos si una coincide  el nombre con alguna de las entradas de la tabla de nombres de secciones y si es así se devuelve su índice de su cabecera
  for(i = 0; i < view->header->e_shnum; i++){
    const char *section_name;

    section_name = xv6_tcc_rel_section_name(view, i);
    if(section_name && strcmp(section_name, name) == 0)
      return i;
  }
  return XV6_TCC_READER_SECTION_NOT_FOUND;
}

/*Devuelve un símbolo de la tabla de símbolos ".symtab" dado su índice en la tabla*/
const struct Xv6TccElfSym *
xv6_tcc_rel_symbol_at(const struct Xv6TccRelObjectView *view,
                       uint index)
{
  if(!view || !view->symbols || index >= view->symbol_count)
    return 0;
  return &view->symbols[index];
}

/*Obtiene el nombre de un símbolo*/
const char *
xv6_tcc_rel_symbol_name(const struct Xv6TccRelObjectView *view,
                         const struct Xv6TccElfSym *symbol)
{
  if(!view || !symbol)
    return 0;
  return string_at(view->strtab, view->strtab_size, symbol->st_name);
}

/*ccede a una relocación de .rela.text dando el índice en la tabla de relocaciones*/
const struct Xv6TccElfRela *
xv6_tcc_rel_relocation_at(const struct Xv6TccRelObjectView *view,
                           uint index)
{
  if(!view || !view->relocations || index >= view->relocation_count)
    return 0;
  return &view->relocations[index];
}

const struct Xv6TccElfRela *
xv6_tcc_rel_rodata_relocation_at(
    const struct Xv6TccRelObjectView *view, uint index)
{
  if(!view || !view->rodata_relocations ||
     index >= view->rodata_relocation_count)
    return 0;
  return &view->rodata_relocations[index];
}

const struct Xv6TccElfRela *
xv6_tcc_rel_data_relocation_at(
    const struct Xv6TccRelObjectView *view, uint index)
{
  if(!view || !view->data_relocations ||
     index >= view->data_relocation_count)
    return 0;
  return &view->data_relocations[index];
}

static int
validate_header(struct Xv6TccRelObjectView *view)
{
  //var auxiliar para acceder a la cabecera elf
  const struct Xv6TccElfHeader *header;
  uint64 table_size;

  /*Se comprueba el tamaño de la cabecera. Lo hago un poco a lo bruto, la cabecera ocupa 64 bytes por el tamaño de Xv6TccElfHeader.
  AHora si TODO el fichero ELF, su tamaño (en el campo size) fuera menor a 64 bytes, entonces sabemos que la cabecera no está compelta*/
  if(view->size < sizeof(struct Xv6TccElfHeader))
    return -1;
  /*Como en view->data tenemos TODO el fichero objeto ELF relocatable. Pero view->data tb apunta al 1er byte del fichero, entonces 
  al hacer (const struct Xv6TccElfHeader *)view->data; hacemos que los primero bytes en "view->ddata" se interpreten como la estructura 
  Xv6TccElfHeader y así poder validar sus campos */
  header = (const struct Xv6TccElfHeader *)view->data;

  if(header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
     header->e_ident[2] != 'L' || header->e_ident[3] != 'F' ||
     header->e_ident[4] != XV6_TCC_ELFCLASS64 || //sea ELF64, un elf32 sería rechazado
     header->e_ident[5] != XV6_TCC_ELFDATA2LSB || //sea little endian
     header->e_ident[6] != XV6_TCC_EV_CURRENT ||
     header->e_type != XV6_TCC_ET_REL ||
     header->e_machine != XV6_TCC_EM_RISCV ||
     header->e_version != XV6_TCC_EV_CURRENT ||
     header->e_entry != 0 || header->e_phoff != 0 || header->e_phnum != 0 ||
     header->e_ehsize != sizeof(struct Xv6TccElfHeader) ||
     header->e_shentsize != sizeof(struct Xv6TccElfSectionHeader) ||
     header->e_shnum == 0 ||
     header->e_shnum > XV6_TCC_READER_MAX_SECTIONS ||
     header->e_shstrndx >= header->e_shnum)
    return -1;

  table_size = (uint64)header->e_shnum *
               sizeof(struct Xv6TccElfSectionHeader);
  if(!range_valid(view->size, header->e_shoff, table_size))
    return -1;

  /*COmo ya se validó la cabecera, se procede a meterla en el campo definitivo de la vista*/
  view->header = header;
  //se mete el header ELF en la tabla de cabeceras de las secciones en la vista
  view->sections = (const struct Xv6TccElfSectionHeader *)(
      view->data + header->e_shoff);
  return 0;
}

static int
prepare_section_names(struct Xv6TccRelObjectView *view)
{
  const struct Xv6TccElfSectionHeader *section;
  int i;

  //saco el índice de la tabla de nombres de secciones
  section = &view->sections[view->header->e_shstrndx];
  //válido que es sección sea la de shstrtab y que esa sección se encuentre dentro del ELF
  if(section->sh_type != XV6_TCC_SHT_STRTAB ||
     section_data_valid(view, section) < 0)
    return -1;

  /*Creo la vista de tabla de nombres de secciones*/
  view->shstrtab_index = view->header->e_shstrndx;
  view->shstrtab_section = section;
  view->shstrtab = (const char *)(view->data + section->sh_offset);
  view->shstrtab_size = section->sh_size;
  //se comprueba que la tabla no esté vacía y que el primer byte sea \0
  if(view->shstrtab_size == 0 || view->shstrtab[0] != 0)
    return -1;

  for(i = 0; i < view->header->e_shnum; i++)
    if(section_data_valid(view, &view->sections[i]) < 0 ||
       !xv6_tcc_rel_section_name(view, i))
      return -1;

  //se comprueba que exista la sección nula (la entrada cero de la tabla de nombres de secciones de ser nula)
  if(view->sections[0].sh_type != XV6_TCC_SHT_NULL)
    return -1;
  return 0;
}

static int
locate_required_sections(struct Xv6TccRelObjectView *view)
{
    /*Busco los índices de las cabeceras de las secciones por su nombre.
    Las busco por su nombre porque el orden de las cabeceras depende de cómo construya el ELF*/
  view->text_index = xv6_tcc_rel_find_section(view, ".text");
  view->rela_text_index = xv6_tcc_rel_find_section(view, ".rela.text");
  view->rodata_index = xv6_tcc_rel_find_section(view, ".rodata");
  view->data_index = xv6_tcc_rel_find_section(view, ".data");
  view->bss_index = xv6_tcc_rel_find_section(view, ".bss");
  view->rela_rodata_index = xv6_tcc_rel_find_section(view, ".rela.rodata");
  view->rela_data_index = xv6_tcc_rel_find_section(view, ".rela.data");
  view->symtab_index = xv6_tcc_rel_find_section(view, ".symtab");
  view->strtab_index = xv6_tcc_rel_find_section(view, ".strtab");

  /*Compruebo que existen las cabeceras (tienen un índice positivo para su posición). Incluso aunque no hayan relocaciones ".rela.tex", debe existir aunque su tamaño sea cero.
  También compruebo que la cabecera de la tabla .shstrtab esté en el lugar correcto*/
  if(view->text_index < 0 || view->rela_text_index < 0 ||
     view->rodata_index < 0 || view->data_index < 0 ||
     view->bss_index < 0 || view->rela_rodata_index < 0 ||
     view->rela_data_index < 0 || view->symtab_index < 0 ||
     view->strtab_index < 0 ||
     xv6_tcc_rel_find_section(view, ".shstrtab") != view->shstrtab_index)
    return -1;

  /*Con los índices de las cabeceras, ya puedo obtener las cabeceras en sí de cada sección*/
  view->text_section = &view->sections[view->text_index];
  view->rela_text_section = &view->sections[view->rela_text_index];
  view->rodata_section = &view->sections[view->rodata_index];
  view->data_section = &view->sections[view->data_index];
  view->bss_section = &view->sections[view->bss_index];
  view->rela_rodata_section = &view->sections[view->rela_rodata_index];
  view->rela_data_section = &view->sections[view->rela_data_index];
  view->symtab_section = &view->sections[view->symtab_index];
  view->strtab_section = &view->sections[view->strtab_index];

  /*Necesito guardar tanto el índice de la cabecera como la cabecera en sí porque es necesario para las relocaciones como:
    sh_link
    sh_info
    st_shndx
    ya que eesos campos guardan números de sección*/
  /*Valido el tipo de la cabecera de cada sección*/
  if(view->text_section->sh_type != XV6_TCC_SHT_PROGBITS ||
     view->text_section->sh_flags !=
         (XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_EXECINSTR) ||
     view->rela_text_section->sh_type != XV6_TCC_SHT_RELA ||
     view->rodata_section->sh_type != XV6_TCC_SHT_PROGBITS ||
     view->rodata_section->sh_flags != XV6_TCC_SHF_ALLOC ||
     view->data_section->sh_type != XV6_TCC_SHT_PROGBITS ||
     view->data_section->sh_flags !=
         (XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_WRITE) ||
     view->bss_section->sh_type != XV6_TCC_SHT_NOBITS ||
     view->bss_section->sh_flags !=
         (XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_WRITE) ||
     view->rela_rodata_section->sh_type != XV6_TCC_SHT_RELA ||
     view->rela_data_section->sh_type != XV6_TCC_SHT_RELA ||
     view->symtab_section->sh_type != XV6_TCC_SHT_SYMTAB ||
     view->strtab_section->sh_type != XV6_TCC_SHT_STRTAB)
    return -1;

  /*Validar relaciones entre las secciones. Las secciones no son independientes, algunas necesitan indicar con cuáles trabajn. EJ  .symtab necesita .strtab*/
  if(view->rela_text_section->sh_link != (uint)view->symtab_index ||
     view->rela_text_section->sh_info != (uint)view->text_index ||
     view->rela_rodata_section->sh_link != (uint)view->symtab_index ||
     view->rela_rodata_section->sh_info != (uint)view->rodata_index ||
     view->rela_data_section->sh_link != (uint)view->symtab_index ||
     view->rela_data_section->sh_info != (uint)view->data_index ||
     view->symtab_section->sh_link != (uint)view->strtab_index)
    return -1;
  return 0;
}

static int
valid_symbol_section(const struct Xv6TccRelObjectView *view, ushort shndx)
{
  return shndx == XV6_TCC_SHN_UNDEF || shndx == view->text_index ||
         shndx == view->rodata_index || shndx == view->data_index ||
         shndx == view->bss_index;
}

static uint64
section_size_for_symbol(const struct Xv6TccRelObjectView *view,
                        ushort shndx)
{
  if(shndx == view->text_index)
    return view->text_section->sh_size;
  if(shndx == view->rodata_index)
    return view->rodata_section->sh_size;
  if(shndx == view->data_index)
    return view->data_section->sh_size;
  if(shndx == view->bss_index)
    return view->bss_section->sh_size;
  return 0;
}

static int
symbol_binding(const struct Xv6TccElfSym *symbol)
{
  return (symbol->st_info >> 4) & 0xf;
}

static int
validate_relocation_table(const struct Xv6TccRelObjectView *view,
                          const struct Xv6TccElfRela *relocations,
                          uint count, uint64 target_size,
                          int data_relocations)
{
  uint i;

  for(i = 0; i < count; i++){
    const struct Xv6TccElfRela *relocation;
    uint symbol_index;
    uint type;
    uint bytes;

    relocation = &relocations[i];
    symbol_index = xv6_tcc_elf_r_symbol(relocation->r_info);
    type = xv6_tcc_elf_r_type(relocation->r_info);
    if(symbol_index >= view->symbol_count)
      return -1;

    if(data_relocations){
      if(type == XV6_TCC_R_RISCV_32)
        bytes = 4;
      else if(type == XV6_TCC_R_RISCV_64)
        bytes = 8;
      else
        return -1;
    } else {
      if(type == XV6_TCC_R_RISCV_CALL)
        bytes = 8;
      else if(type == XV6_TCC_R_RISCV_BRANCH ||
              type == XV6_TCC_R_RISCV_JAL ||
              type == XV6_TCC_R_RISCV_PCREL_HI20 ||
              type == XV6_TCC_R_RISCV_PCREL_LO12_I)
        bytes = 4;
      else
        return -1;
    }

    if(relocation->r_offset > target_size ||
       bytes > target_size - relocation->r_offset)
      return -1;
  }
  return 0;
}

static int
prepare_tables(struct Xv6TccRelObjectView *view)
{
  uint i;

  if(view->symtab_section->sh_entsize != sizeof(struct Xv6TccElfSym) ||
     view->symtab_section->sh_size % sizeof(struct Xv6TccElfSym) != 0 ||
     view->rela_text_section->sh_entsize != sizeof(struct Xv6TccElfRela) ||
     view->rela_text_section->sh_size % sizeof(struct Xv6TccElfRela) != 0 ||
     view->rela_rodata_section->sh_entsize != sizeof(struct Xv6TccElfRela) ||
     view->rela_rodata_section->sh_size % sizeof(struct Xv6TccElfRela) != 0 ||
     view->rela_data_section->sh_entsize != sizeof(struct Xv6TccElfRela) ||
     view->rela_data_section->sh_size % sizeof(struct Xv6TccElfRela) != 0)
    return -1;

  view->text = view->data + view->text_section->sh_offset;
  view->rodata = view->data + view->rodata_section->sh_offset;
  view->data_bytes = view->data + view->data_section->sh_offset;
  view->bss_size = view->bss_section->sh_size;
  view->symbols = (const struct Xv6TccElfSym *)(
      view->data + view->symtab_section->sh_offset);
  view->symbol_count = view->symtab_section->sh_size /
                       sizeof(struct Xv6TccElfSym);
  view->relocations = (const struct Xv6TccElfRela *)(
      view->data + view->rela_text_section->sh_offset);
  view->relocation_count = view->rela_text_section->sh_size /
                           sizeof(struct Xv6TccElfRela);
  view->rodata_relocations = (const struct Xv6TccElfRela *)(
      view->data + view->rela_rodata_section->sh_offset);
  view->rodata_relocation_count = view->rela_rodata_section->sh_size /
                                  sizeof(struct Xv6TccElfRela);
  view->data_relocations = (const struct Xv6TccElfRela *)(
      view->data + view->rela_data_section->sh_offset);
  view->data_relocation_count = view->rela_data_section->sh_size /
                                sizeof(struct Xv6TccElfRela);
  view->strtab = (const char *)(view->data + view->strtab_section->sh_offset);
  //Se guarda el número total de bytes que ocupa .strtab.
  view->strtab_size = view->strtab_section->sh_size;

  if(view->strtab_size == 0 || view->strtab[0] != 0 ||
     view->symbol_count == 0 ||
     view->symtab_section->sh_info == 0 ||
     view->symtab_section->sh_info > view->symbol_count)
    return -1;

  if(view->symbols[0].st_name != 0 || view->symbols[0].st_info != 0 ||
     view->symbols[0].st_other != 0 ||
     view->symbols[0].st_shndx != XV6_TCC_SHN_UNDEF ||
     view->symbols[0].st_value != 0 || view->symbols[0].st_size != 0)
    return -1;

  /*
  Se recorren todos los símbolos para validar sus nombres y la sección
  a la que pertenecen.
  */
  for(i = 0; i < view->symbol_count; i++){
    const struct Xv6TccElfSym *symbol;
    int binding;

    //Se obtiene el símbolo situado en el índice i de .symtab.
    symbol = &view->symbols[i];
    binding = symbol_binding(symbol);
    if(!string_at(view->strtab, view->strtab_size, symbol->st_name) ||
       !valid_symbol_section(view, symbol->st_shndx) ||
       (binding != XV6_TCC_STB_LOCAL &&
        binding != XV6_TCC_STB_GLOBAL &&
        binding != XV6_TCC_STB_WEAK))
      return -1;
    if(i < view->symtab_section->sh_info && binding != XV6_TCC_STB_LOCAL)
      return -1;
    if(i >= view->symtab_section->sh_info && binding == XV6_TCC_STB_LOCAL)
      return -1;
    /*
    Se valida st_shndx, que indica en qué sección está definido el símbolo.

    Se permite SHN_UNDEF porque un símbolo puede estar sin definir en este
    objeto y ser resuelto posteriormente por el linker utilizando otro
    fichero objeto.

    Si el símbolo está definido, st_shndx debe ser un índice válido dentro
    de la tabla de cabeceras de sección.
    */
    if(symbol->st_shndx != XV6_TCC_SHN_UNDEF &&
       symbol->st_value > section_size_for_symbol(view, symbol->st_shndx))
      return -1;
  }

  if(validate_relocation_table(view, view->relocations,
                               view->relocation_count,
                               view->text_section->sh_size, 0) < 0 ||
     validate_relocation_table(view, view->rodata_relocations,
                               view->rodata_relocation_count,
                               view->rodata_section->sh_size, 1) < 0 ||
     validate_relocation_table(view, view->data_relocations,
                               view->data_relocation_count,
                               view->data_section->sh_size, 1) < 0)
    return -1;
  return 0;
}

int
xv6_tcc_parse_rel_object(const uchar *data, uint size,
                          struct Xv6TccRelObjectView *view)
{
  //Se rechazan los punteros nuloss
  if(!data || !view)
    return -1;
  /*
  Se limpia completamente la estructura de salida

  Esto pone inicialmente a cero:
  - todos los punteros;
  - todos los tamaños;
  - todos los contadores;
  - todos los índices.

  De esta forma no quedan valores antiguos si view había sido utilizado
  anteriormente
  */
  memset(view, 0, sizeof(*view));
  /*
  Se guarda en la vista el buffer completo que contiene el objeto ELF
  y el número de bytes válidos que contiene

  view->data apunta al mismo buffer recibido en data.
  */
  view->data = data;
  view->size = size;

  if(validate_header(view) < 0 || prepare_section_names(view) < 0 ||
     locate_required_sections(view) < 0 || prepare_tables(view) < 0){
    memset(view, 0, sizeof(*view));
    return -1;
  }
  return 0;
}

int
xv6_tcc_load_rel_object(const char *path,
                         struct Xv6TccElfBuffer *storage,
                         struct Xv6TccRelObjectView *view)
{
  int file;
  int total;

  if(!path || !storage || !storage->data || !view)
    return -1;
  //Se abre el fichero objeto en modo de solo lectura
  file = open(path, O_RDONLY);
  if(file < 0)
    return -1;

  total = 0;
  while(total < (int)storage->capacity){
    int amount;

    amount = read(file, storage->data + total, storage->capacity - total);
    /*
    Un resultado negativo indica un error de lectura
    Antes de devolver error se cierra el descriptor
    */
    if(amount < 0){
      close(file);
      return -1;
    }
    /*
    read() devuelve cero cuando se ha alcanzado el final del fichero

    En ese momento ya se han cargado todos sus bytes y se sale del bucle
    */
    if(amount == 0)
      break;
    total += amount;
  }

  if(total == (int)storage->capacity){
    uchar extra;
    int amount;

    amount = read(file, &extra, 1);
    /*
    Si read() devuelve cero, el fichero terminaba exactamente al llenar
    el buffer y es válido

    Si devuelve uno, existe al menos otro byte y el fichero es demasiado
    grande.

    Si devuelve un valor negativo, ocurrió un error de lectura

    En los dos últimos casos amount será distinto de cero y se devuelve
    error
    */
    if(amount != 0){
      close(file);
      return -1;
    }
  }
  /*
  Se cierra el fichero después de haber leído todos sus bytes

  Si close() falla, también se devuelve error
  */
  if(close(file) < 0)
    return -1;

  storage->size = total;
  return xv6_tcc_parse_rel_object(storage->data, storage->size, view);
}
