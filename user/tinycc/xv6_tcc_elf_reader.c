/*
xv6_tcc_elf_reader.c

Lector educativo de objetos ELF64 RISC-V ET_REL.
Valida la cabecera, la tabla de secciones, las cadenas, los simbolos y las
relocaciones producidas por asxv6. La organizacion se inspira en la lectura
de objetos de TinyCC tccelf.c..

Donante conceptual:
  TinyCC tccelf.c
*/

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_reader.h"

#define XV6_TCC_SHT_NOBITS 8

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

/*Comprueba que los datos descritos por una sección se encuentre en una zona válida dentro del fichero
y para ello utiliza la función "range_Valid".

Antes de esa comprobación, se ha puesto un IF con XV6_TCC_SHT_NOBITS que se utiliza para la sección ".bss", aún no la tenemos pero se válida desde ya:
una sección .bss implica que tiene un tamaño en memoria pero no guarda todos esos ceros de su tamaño en memoria en el fichero objeto (no los escribe).
LO único que hacemos aquí es comprobar que la sección .bss no se salga del fichero sh_offset + sh_size <= file_size*/
static int
section_data_valid(const struct Xv6TccRelObjectView *view,
                   const struct Xv6TccElfSectionHeader *section)
{
  if(section->sh_type == XV6_TCC_SHT_NOBITS)
    return section->sh_offset <= view->size;

  return range_valid(view->size, section->sh_offset, section->sh_size);
}

/*Devuelve el nombre de una sección en la tabla de nombres de secciones "shstrtab" dando su índice*/
const char *
xv6_tcc_rel_section_name(const struct Xv6TccRelObjectView *view,
                          int section_index)
{
  /*Primero comprueba:
    el objeto auxiliar vista no sea nulo
    cabecera validada (no nulo)
    tabla de secciones (no nulo)
    tabla de nombres (no nulo)
    índice no negativo
    índice menor que el número de secciones */
  if(!view || !view->header || !view->sections ||
     !view->shstrtab || section_index < 0 ||
     section_index >= view->header->e_shnum)
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

  //si no se encuentra la sección
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

/*Primera función de comprobación del ELF relocatable: comprueba que la cabecera del ELF esté válida*/
static int
validate_header(struct Xv6TccRelObjectView *view)
{
  //var auxiliar para acceder a la cabecera elf
  const struct Xv6TccElfHeader *header;
  uint64 section_table_size;

  /*Se comprueba el tamaño de la cabecera. Lo hago un poco a lo bruto, la cabecera ocupa 64 bytes por el tamaño de Xv6TccElfHeader.
  AHora si TODO el fichero ELF, su tamaño (en el campo size) fuera menor a 64 bytes, entonces sabemos que la cabecera no está compelta*/
  if(view->size < sizeof(struct Xv6TccElfHeader))
    return -1;

  /*Como en view->data tenemos TODO el fichero objeto ELF relocatable. Pero view->data tb apunta al 1er byte del fichero, entonces 
  al hacer (const struct Xv6TccElfHeader *)view->data; hacemos que los primero bytes en "view->ddata" se interpreten como la estructura 
  Xv6TccElfHeader y así poder validar sus campos */
  header = (const struct Xv6TccElfHeader *)view->data;

  //se valida que los primeros cuatro bytes deben sean el # mágico ELF: 0x7f 'E' 'L' 'F'
  if(header->e_ident[0] != 0x7f ||
     header->e_ident[1] != 'E' ||
     header->e_ident[2] != 'L' ||
     header->e_ident[3] != 'F' ||
     header->e_ident[4] != XV6_TCC_ELFCLASS64 || //sea ELF64, un elf32 sería rechazado
     header->e_ident[5] != XV6_TCC_ELFDATA2LSB || //sea little endian
     header->e_ident[6] != XV6_TCC_EV_CURRENT)
    return -1;

  //se valida el tipo de objeto
  if(header->e_type != XV6_TCC_ET_REL ||
     header->e_machine != XV6_TCC_EM_RISCV ||
     header->e_version != XV6_TCC_EV_CURRENT ||
     header->e_ehsize != sizeof(struct Xv6TccElfHeader) ||
     header->e_phoff != 0 || header->e_phnum != 0 ||
     header->e_shentsize != sizeof(struct Xv6TccElfSectionHeader) ||
     header->e_shnum == 0 ||
     header->e_shnum > XV6_TCC_READER_MAX_SECTIONS ||
     header->e_shstrndx >= header->e_shnum)
    return -1;

  /*Se validan que la tabla de nombres de secciones shstrtab esté dentro del ELF (el tamaño, y no se salga)*/
  section_table_size = (uint64)header->e_shnum * sizeof(struct Xv6TccElfSectionHeader);
  if(!range_valid(view->size, header->e_shoff, section_table_size))
    return -1;

  /*COmo ya se validó la cabecera, se procede a meterla en el campo definitivo de la vista*/
  view->header = header;
  //se mete el header ELF en la tabla de cabeceras de las secciones en la vista
  view->sections = (const struct Xv6TccElfSectionHeader *)(
      view->data + header->e_shoff);
  return 0;
}

/*Valida que exista y esté correcta la tabla de nombres de secciones shstrtab y luego 
la guarda en el campo definitivo de la vista*/
static int
prepare_section_names(struct Xv6TccRelObjectView *view)
{
  const struct Xv6TccElfSectionHeader *section;
  int i;

  //saco el índice de la tabla de nombres de secciones
  section = &view->sections[view->header->e_shstrndx];

  //válido que es sección sea la de shstrtab y que esa sección se encuentre dentro del ELF
  if(section->sh_type != XV6_TCC_SHT_STRTAB ||
     !section_data_valid(view, section))
    return -1;

  /*Creo la vista de tabla de nombres de secciones*/
  view->shstrtab_index = view->header->e_shstrndx;
  view->shstrtab_section = section;
  view->shstrtab = (const char *)(view->data + section->sh_offset);
  view->shstrtab_size = section->sh_size;

  //se comprueba que la tabla no esté vacía y que el primer byte sea \0
  if(view->shstrtab_size == 0 || view->shstrtab[0] != 0)
    return -1;

  /*se validan las cabeceras de cada sección
  1. Que la cabecera de la sección se encuentre dentro del fichero objeto 
  2. QUe el nombre de esa sección se encuentre en la tabla de de nombres de secciones shstrtab*/
  for(i = 0; i < view->header->e_shnum; i++){
    if(!section_data_valid(view, &view->sections[i]) || !xv6_tcc_rel_section_name(view, i))
      return -1;
  }

  //se comprueba que exista la sección nula (la entrada cero de la tabla de nombres de secciones de ser nula)
  if(view->sections[0].sh_type != XV6_TCC_SHT_NULL)
    return -1;
  return 0;
}

/*Válida las cabeceras de las secciones del ELF  (que las secciones en su header tengan su tipo y relaciones correctas), guarda los índices de los headers y los headers en sí
en "view"*/
static int
locate_required_sections(struct Xv6TccRelObjectView *view)
{
    /*Busco los índices de las cabeceras de las secciones por su nombre.
    Las busco por su nombre porque el orden de las cabeceras depende de cómo construya el ELF*/
  view->text_index = xv6_tcc_rel_find_section(view, ".text");
  view->rela_text_index = xv6_tcc_rel_find_section(view, ".rela.text");
  view->symtab_index = xv6_tcc_rel_find_section(view, ".symtab");
  view->strtab_index = xv6_tcc_rel_find_section(view, ".strtab");

  /*Compruebo que existen las cabeceras (tienen un índice positivo para su posición). Incluso aunque no hayan relocaciones ".rela.tex", debe existir aunque su tamaño sea cero.
  También compruebo que la cabecera de la tabla .shstrtab esté en el lugar correcto*/
  if(view->text_index < 0 || view->rela_text_index < 0 ||
     view->symtab_index < 0 || view->strtab_index < 0 ||
     xv6_tcc_rel_find_section(view, ".shstrtab") !=
         view->shstrtab_index)
    return -1;

  /*Con los índices de las cabeceras, ya puedo obtener las cabeceras en sí de cada sección*/
  view->text_section = &view->sections[view->text_index];
  view->rela_text_section = &view->sections[view->rela_text_index];
  view->symtab_section = &view->sections[view->symtab_index];
  view->strtab_section = &view->sections[view->strtab_index];

  /*Necesito guardar tanto el índice de la cabecera como la cabecera en sí porque es necesario para las relocaciones como:
    sh_link
    sh_info
    st_shndx
    ya que eesos campos guardan números de sección*/

  /*Valido el tipo de la cabecera de cada sección*/
  if(view->text_section->sh_type != XV6_TCC_SHT_PROGBITS ||
     (view->text_section->sh_flags &
      (XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_EXECINSTR)) !=
      (XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_EXECINSTR) ||
     view->rela_text_section->sh_type != XV6_TCC_SHT_RELA ||
     view->symtab_section->sh_type != XV6_TCC_SHT_SYMTAB ||
     view->strtab_section->sh_type != XV6_TCC_SHT_STRTAB)
    return -1;

  /*Validar relaciones entre las secciones. Las secciones no son independientes, algunas necesitan indicar con cuáles trabajn. EJ  .symtab necesita .strtab*/
  if(view->rela_text_section->sh_link != (uint)view->symtab_index ||
     view->rela_text_section->sh_info != (uint)view->text_index ||
     view->symtab_section->sh_link != (uint)view->strtab_index)
    return -1;

    //diagram completo de relaciones posiblemente meterlo en memoria
  return 0;
}

/*Convierte las secciones localizadas mediante sus cabeceras
en vistas para acceder a su contenido. La vista es una estructura auxiliar 
para observar los campos del fichero ELF relocatable.

En locate_required_sections() ya se obtuvieron las cabeceras de:
- .text
- .symtab
- .strtab
- .rela.text

En esta función se utiliza el campo sh_offset de cada cabecera para obtener
un puntero hacia los bytes reales de cada sección dentro del fichero ELF.*/
static int
prepare_tables(struct Xv6TccRelObjectView *view)
{
  uint i;

  /*Se valida el formato de la sección de la tabla de símbolos symtab
  --> cada ebtrada de 
  sh_entsize debe indicar que cada entrada ocupa exactamente lo mismo
  que una estructura Xv6TccElfSym.

  Además, el tamaño total de .symtab debe ser múltiplo del tamaño de
  una entrada. Por ejemplo:

      sh_size = 96 bytes
      sizeof(Xv6TccElfSym) = 24 bytes

      96 % 24 = 0

  Por lo tanto, la tabla contiene cuatro símbolos completos.

  Si el resto fuera distinto de cero significaría que al final de la
  sección hay una entrada incompleta o bytes sobrantes.
  */
  if(view->symtab_section->sh_entsize !=
         sizeof(struct Xv6TccElfSym) ||
     view->symtab_section->sh_size %
         sizeof(struct Xv6TccElfSym) != 0 ||

     /*
     Se realiza la misma validación para .rela.text.

     Cada entrada de relocación debe ocupar exactamente el tamaño de
     Xv6TccElfRela y el tamaño total de la sección debe ser múltiplo
     de ese tamaño.
     */
     view->rela_text_section->sh_entsize !=
         sizeof(struct Xv6TccElfRela) ||
     view->rela_text_section->sh_size %
         sizeof(struct Xv6TccElfRela) != 0)
    return -1;

  /*
  Se obtiene un puntero a los bytes reales de .text.

  text_section->sh_offset contiene el desplazamiento desde el comienzo
  del fichero ELF hasta el primer byte de .text.

  Ejemplo:

      view->data apunta al comienzo del ELF
      sh_offset = 64

      view->text = view->data + 64

  view->text apunta ahora directamente a las instrucciones RISC-V.
  */
  view->text =
      view->data + view->text_section->sh_offset;

  /*
  Se obtiene un puntero a la tabla de símbolos.

  Los bytes que comienzan en el sh_offset de .symtab se interpretan
  como un array de estructuras Xv6TccElfSym.

  No se crea una copia de los símbolos. view->symbols apunta dentro
  del propio fichero almacenado en view->data.
  */
  view->symbols =
      (const struct Xv6TccElfSym *)(
          view->data + view->symtab_section->sh_offset);

  /*
  Se calcula el número de entradas de la tabla de símbolos:

      número de símbolos =
          tamaño total de .symtab /
          tamaño de una entrada Xv6TccElfSym
  */
  view->symbol_count =
      view->symtab_section->sh_size /
      sizeof(struct Xv6TccElfSym);

  /*
  Se obtiene un puntero a las entradas de .rela.text.

  Los bytes de esa sección se interpretan como un array de estructuras
  Xv6TccElfRela.
  */
  view->relocations =
      (const struct Xv6TccElfRela *)(
          view->data + view->rela_text_section->sh_offset);

  /*
  Se calcula el número de entradas de relocación:

      número de relocaciones =
          tamaño de .rela.text /
          tamaño de una entrada Xv6TccElfRela
  */
  view->relocation_count =
      view->rela_text_section->sh_size /
      sizeof(struct Xv6TccElfRela);

  /*
  Se obtiene un puntero a la tabla de nombres de símbolos .strtab.

  strtab_section->sh_offset indica dónde comienza la tabla dentro del
  fichero objeto.
  */
  view->strtab =
      (const char *)(
          view->data + view->strtab_section->sh_offset);

  //Se guarda el número total de bytes que ocupa .strtab.
  view->strtab_size = view->strtab_section->sh_size;

  /*
  Una tabla de cadenas ELF debe contener al menos el byte nulo inicial.

  El offset 0 representa la cadena vacía. Por eso .strtab no puede estar
  vacía y su primer byte debe ser '\0'.
  */
  if(view->strtab_size == 0 || view->strtab[0] != 0)
    return -1;

  /*
  La tabla de símbolos debe contener al menos la entrada nula de índice 0.

  Además, sh_info de .symtab indica el índice del primer símbolo no local,
  por lo que no puede ser mayor que el número total de símbolos.

  Se permite que sea igual a symbol_count. En ese caso no habría símbolos
  globales y todos los símbolos serían locales.
  */
  if(view->symbol_count == 0 ||
     view->symtab_section->sh_info > view->symbol_count)
    return -1;

  /*
  La primera entrada de .symtab debe ser el símbolo nulo obligatorio.

  Todos sus campos deben valer cero y st_shndx debe ser SHN_UNDEF.

  Este símbolo no representa ninguna etiqueta o función real. Se reserva
  porque el índice de símbolo 0 tiene significado especial en ELF.
  */
  if(view->symbols[0].st_name != 0 ||
     view->symbols[0].st_info != 0 ||
     view->symbols[0].st_other != 0 ||
     view->symbols[0].st_shndx != XV6_TCC_SHN_UNDEF ||
     view->symbols[0].st_value != 0 ||
     view->symbols[0].st_size != 0)
    return -1;

  /*
  Se recorren todos los símbolos para validar sus nombres y la sección
  a la que pertenecen.
  */
  for(i = 0; i < view->symbol_count; i++){
    const struct Xv6TccElfSym *symbol;

    //Se obtiene el símbolo situado en el índice i de .symtab.
    symbol = &view->symbols[i];

    /*
    Se comprueba que st_name apunte a una cadena válida dentro de .strtab.

    xv6_tcc_rel_symbol_name() valida:
    - que el offset st_name esté dentro de .strtab;
    - que exista un '\0' antes del final de la tabla.
    */
    if(!xv6_tcc_rel_symbol_name(view, symbol))
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
       symbol->st_shndx >= view->header->e_shnum)
      return -1;
  }

  /*
  Se recorren todas las entradas de .rela.text para comprobar que sean
  relocaciones válidas para esta versión del ensamblador y linker.
  */
  for(i = 0; i < view->relocation_count; i++){
    const struct Xv6TccElfRela *relocation;
    uint symbol_index;
    uint type;

    //Se obtiene la entrada de relocación situada en el índice i
    relocation = &view->relocations[i];

    /*
    r_info contiene dos valores empaquetados:

      bits 63..32: índice del símbolo dentro de .symtab
      bits 31..0:  tipo de relocación

    Aquí se extraen ambos valores mediante los helpers ELF.
    */
    symbol_index =
        xv6_tcc_elf_r_symbol(relocation->r_info);
    type =
        xv6_tcc_elf_r_type(relocation->r_info);

    /*
    Se comprueba que el índice del símbolo exista en .symtab.

    También se valida r_offset, que indica dónde se aplicará la
    relocación dentro de .text.

    Primero se comprueba que r_offset no sea mayor que el tamaño de .text.
    Después se comprueba que desde ese offset queden al menos cuatro bytes,
    porque las relocaciones soportadas modifican una instrucción RISC-V
    completa de 32 bits.

    Ejemplo válido:

      .text mide 16 bytes
      r_offset = 12

      16 - 12 = 4 bytes disponibles

    Ejemplo inválido:

      .text mide 16 bytes
      r_offset = 14

      16 - 14 = 2 bytes disponibles
    */
    if(symbol_index >= view->symbol_count ||
       relocation->r_offset > view->text_section->sh_size ||
       view->text_section->sh_size -
           relocation->r_offset < 4)
      return -1;

    /*
    De momento el ensamblador solo genera dos tipos de relocación:

    - R_RISCV_BRANCH para instrucciones condicionales como beq o bne.
    - R_RISCV_JAL para saltos y llamadas codificados con jal.

    Cualquier otro tipo se rechaza porque todavía no está implementado
    por este linker educativo.
    */
    if(type != XV6_TCC_R_RISCV_BRANCH &&
       type != XV6_TCC_R_RISCV_JAL)
      return -1;
  }

  return 0;
}

/*
Función principal para analizar un objeto ELF64 RISC-V ET_REL que ya
se encuentra cargado completamente en memoria

Recibe:
- data: puntero al primer byte del fichero objeto;
- size: número total de bytes del fichero;
- view: estructura donde se guardará la vista validada del objeto.

Esta función no abre ni lee ningún fichero. Solo analiza los bytes que
ya existen en data.

El análisis se divide en cuatro fases:
1. Validar la cabecera ELF.
2. Preparar y validar la tabla de nombres de secciones .shstrtab.
3. Localizar y validar las secciones obligatorias.
4. Preparar y validar .text, .symtab, .strtab y .rela.text.

Si todas las fases funcionan, view queda preparado con punteros que
apuntan directamente a regiones interiores de data
*/
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

  /*
  Los índices de sección se inicializan explícitamente a -1 para indicar
  que todavía no se ha encontrado ninguna de esas secciones

  No se puede utilizar 0 como valor de "no encontrado" porque el índice 0
  ya existe y corresponde a la sección nula
  */
  view->text_index =
      XV6_TCC_READER_SECTION_NOT_FOUND;
  view->rela_text_index =
      XV6_TCC_READER_SECTION_NOT_FOUND;
  view->symtab_index =
      XV6_TCC_READER_SECTION_NOT_FOUND;
  view->strtab_index =
      XV6_TCC_READER_SECTION_NOT_FOUND;
  view->shstrtab_index =
      XV6_TCC_READER_SECTION_NOT_FOUND;

  /*
  Se ejecutan las cuatro fases de validación en orden.

  El operador || utiliza evaluación de cortocircuito. Esto significa que
  si una función falla, las siguientes no se ejecutan.

  El orden es necesario:

  - validate_header() debe ejecutarse primero porque prepara view->header
    y view->sections

  - prepare_section_names() necesita la cabecera y las cabeceras de
    sección para preparar .shstrtab

  - locate_required_sections() necesita .shstrtab para buscar las
    secciones por sus nombres

  - prepare_tables() necesita que las cabeceras de .text, .symtab,
    .strtab y .rela.text ya estén localizadas
  */
  if(validate_header(view) < 0 ||
     prepare_section_names(view) < 0 ||
     locate_required_sections(view) < 0 ||
     prepare_tables(view) < 0)
    return -1;

  //Todas las fases se completaron y el objeto es válido.
  return 0;
}

/*
Carga desde el sistema de ficheros un objeto ELF relocatable y después
lo analiza mediante xv6_tcc_parse_rel_object()

Recibe:
- path: nombre o ruta del fichero objeto que se quiere abrir
- storage: buffer donde se copiarán todos los bytes del fichero
- view: estructura donde se guardará la vista ELF validada

storage->data debe apuntar previamente a una región de memoria válida y
storage->capacity debe indicar cuántos bytes caben en esa región

La función:
1. Abre el fichero en modo de solo lectura
2. Lee sus bytes dentro de storage->data
3. Comprueba que el fichero no supere la capacidad del buffer
4. Cierra el descriptor
5. Analiza y valida los bytes cargados
*/
int
xv6_tcc_load_rel_object(const char *path,
                         struct Xv6TccElfBuffer *storage,
                         struct Xv6TccRelObjectView *view)
{
  int file;

  /*
  Se validan los argumentos

  - una ruta válida
  - una estructura de almacenamiento
  - una región real de memoria en storage->data
  - una estructura donde devolver la vista
  - una capacidad distinta de cero
  */
  if(!path || !storage || !storage->data || !view ||
     storage->capacity == 0)
    return -1;

  //Se abre el fichero objeto en modo de solo lectura
  file = open(path, O_RDONLY);
  if(file < 0)
    return -1;

  /*
  Antes de comenzar la lectura se indica que todavía no hay bytes válidos
  dentro del buffer

  storage->capacity no cambia porque representa el tamaño máximo reservado.
  */
  storage->size = 0;

  /*
  Se continúa leyendo mientras quede espacio libre en el buffer

  storage->size indica el primer offset libre

  storage->capacity - storage->size indica cuántos bytes quedan disponibles
  */
  while(storage->size < storage->capacity){
    int amount;

    /*
    Los nuevos bytes se escriben después de los que ya fueron leídos:

        storage->data + storage->size

    Ejemplo:

        size = 100
        capacity = 1000

        la lectura comienza en data + 100
        como máximo se pueden leer 900 bytes
    */
    amount = read(file,
             storage->data + storage->size,
             storage->capacity - storage->size);

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

    //Se actualiza el número de bytes válidos dentro del buffer
    storage->size += amount;
  }

  /*
  Si size es igual a capacity, el buffer quedó completamente lleno

  En ese caso todavía no sabemos si:
  - el fichero mide exactamente capacity bytes;
  - o existen más bytes que ya no caben en el buffer.

  Para distinguir ambos casos se intenta leer un byte adicional
  */
  if(storage->size == storage->capacity){
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

  /*
  Finalmente se analizan los bytes cargados

  storage->data contiene el fichero completo y storage->size indica
  cuántos bytes se leyeron realmente

  xv6_tcc_parse_rel_object() validará el ELF y rellenará view con los
  punteros hacia sus secciones, símbolos y relocaciones
  */
  return xv6_tcc_parse_rel_object(
      storage->data, storage->size, view);
}
