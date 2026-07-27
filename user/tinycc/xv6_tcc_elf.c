/*
  xv6_tcc_elf.c: sirve para generar un fichero ELF
 
  Extracción reducida de las funciones de tccelf.c de TinyCC:
    section_add()
    section_ptr_add()
    put_elf_str()
    put_elf_sym()
 
  Función original:
  https://raw.githubusercontent.com/TinyCC/tinycc/d9d02c56401e43be43760b63f7d82f771a7ed1f6/tccelf.c
 
  Los arrays dinámicos y hashes de TinyCC se sustituyen por buffers con
  capacidad fijada por la capa xv6. Se excluyen ELF dinámico, GOT/PLT,
  versiones, TLS y DWARF.
 

  De momento no se genera un fichero ELF completo. Lo que hay de momento es la infraestructura necesaria 
  para ir construyendo en memoria el contenido que más adelante formará parte del fichero ELF (que tiene todavía 
  más cosas):
    - Código máquína: buffer de .text ----> tipo Xv6TccElfBuffer
    - NOmbre de símbolos (etiquetas): buffer de .strtab ----> tipo Xv6TccElfStringTable
    - nombres de secciones (.text, .symtab): buffer de .shstrtab ----> tipo Xv6TccElfStringTable
    - INformación sobre símbolos: buffer de .symtab ------> tipo Xv6TccElfBuffer. Una entrada del buffer de símbolos se representa con Xv6TccElfSym
    - Relocaciones pendientes: buffer de .rela.text ---> tipo Xv6TccElfRela

    EJ: se parte de este código:
    .text
    .globl main

    main:
        call imprimir
        ret
---------------------------
    EL resultado del ensamblador que buscamos debería generar algo así:
    .text
    bytes de call imprimir
    bytes de ret

    .strtab
        "\0main\0imprimir\0"

    .symtab
        símbolo 0: nulo
        símbolo 1: main
        símbolo 2: imprimir

    .rela.text
        en el offset 0 de .text hay que corregir la llamada a imprimir
  Licencia: GNU LGPL 2.1 o posterior. 
 */

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf.h"

/*EL módulo ELF de tinyCC se usará tanto para el assembler como el linker*/

/*Redondea hacía arriba hasta el siguiente múltiplo de align */
static uint
align_up(uint value, uint align)
{
  if(align <= 1)
    return value;

  //esta función sirve cuando "align" es potencia de 2
  return (value + align - 1) & ~(align - 1);
}

/*Esta función reserva una región dentro del buffer de datos de una sección.
Va llenando el buffer de elementos de una sección (en el campo data del buffer), para ello:
  - el buffer de la sección
  - se manda el nḿero de bytes a agregar
  - alineación requerida 
  - el offset  = aquí se devolvertá el offset dentro del buffer dónde el llamador podrá insertar 
  el nuevo elemento

  Está función hace lo sguiente en este orden:
  - cálcula donde van a comenzar a meterse los nuevos elementos respetando la alineación (hace padding)
  - Reserva lógicamente esos bytes con el buffer con section->size
  - La región reservada dónde se meterán los neuvos elementos los inicializa a cero
  
  LO que no sabe esta función es el significa de los bytes almacenados, no sabe si representan:
  una instrucción, un símbolo, una relocación, una cadena

  argumentos:
  section = buffer  (ya tiene tamaño fijo pero no todo está ocupado)
  bytes = numero de bytes nuevos a reservar internamente
  align = elemento donde se comenzara a meter a información dentro del buffer
  offset = aquí se devolvertá el offset dentro del buffer dónde comienza este nuevo elemento insertado
  */
int
xv6_tcc_section_add(struct Xv6TccElfBuffer *section,
                    uint bytes, uint align, uint *offset)
{
  uint start; //var auxiliar para saber donde se comenzara dentro del buffer a resservar espacio
  uint end; //offset del nuevo final del buffer una vez metido los datos

  //valido punteros 
  if(!section || !section->data || !offset)
    return -1;
  //validar alineación para comprobar que sea potencia de 2
  if(align == 0 || (align & (align - 1)) != 0)
    return -1;

  //se calcula dónde se comenzará la reserva para nuevos elementos del buffer
  start = align_up(section->size, align);
  //se comprueba la capacidad del buffer para ver si entra la nueva info
  //se podría simplificar así (start + bytes > capacity)
  if(start > section->capacity || bytes > section->capacity - start)
    return -1;

  //cálculo del nuevo final dentro del buffer 
  end = start + bytes;
  //antes de meter lo nuevo, se cálcula si se necesita un padding (relleno a cero) para que la info a meter 
  //esté alineada
  if(start > section->size)
    memset(section->data + section->size, 0, start - section->size);
  //esta nueva región reservada se ponea  cero en memoria para que luego el llamador pueda escribir el contenido real en ella
  if(bytes)
    memset(section->data + start, 0, bytes);

  //la región recién reservada pasa a formar parte de la sección
  section->size = end;
  //se le devuelve el offset al llamador para que pueda escribir el elemento
  *offset = start;
  return 0;
}

/*Basada en put_elf_str() de TInyCC
Añade una cadena a la tabla de cadenas ELF "strab",
cada cadena termina con \0

parámetors:
- la tabla de cadenas
- la cadena
- el offset: es el valor que devolverá que indica en donde comienza la cadena agregada en la tabla 

EJ: posición: 0 1 2 3 4 5
  contenido: \0 m a i n \0
  
  Agrego "message"
  entonces quedaría 
  posición: 0 1 2 3 4 5  6 7 8 9 10 11 12 13
contenido: \0 m a i n \0 m e s s  a  g  e \0
y el offset = 6*/
int
xv6_tcc_put_elf_str(struct Xv6TccElfStringTable *table,
                    const char *text, uint *offset)
{
  uint length;

  if(!table || !table->data || !text || !offset)
    return -1;

  //se calcula la longitud de la cadena (se le suma 1 por el nulo)
  length = strlen(text) + 1;
  //se comprueba que el buffer no esté lleno y que la cadena logre entrar
  if(table->size > table->capacity || length > table->capacity - table->size)
    return -1;

  //guarda el offset inicial antes de meter la cadena, ya que ahí es donde iniciará la cadena a meter
  *offset = table->size;
  //copia la cadena en la tabla
  memmove(table->data + table->size, text, length);
  //actualizo el tamaño ocupado de la tabla
  table->size += length;
  return 0;
}

/*Guarda un símbolo en el buffer de símbolos "symtab" cuando ya se ha guardado 
el offset en la tabla de cadenas ELF "srtab". 
SE llama "raw" la función porque aquí se manda el offset dentro del buffer de cadenas en vez de mandar el nombre.

*/
int
xv6_tcc_put_elf_sym_raw(struct Xv6TccElfBuffer *symtab,
                        uint name_offset, uint64 value, uint64 size,
                        int info, int other, int shndx, uint *index)
{
  struct Xv6TccElfSym *symbol;
  uint symbol_offset;

  if(!symtab || !index)
    return -1;

  /*se reserva espacio en la tabla de símbolos para una entrada Elf64_sym
  la entrada comenzaará en un offset alineado a 8 bytes, en symbol_offset se recibirá 
  el desplazamiento a la nueva región reservada*/
  if(xv6_tcc_section_add(symtab, sizeof(*symbol),
                         sizeof(uint64), &symbol_offset) < 0)
    return -1;

  /*COnvierto la dirección de la región reservada (la dirección donde van los datos) en un puntero a Xv6TccElfSym 
  para así poder meter los datos*/
  symbol = (struct Xv6TccElfSym *)(symtab->data + symbol_offset);
  //meto los datos relativos al símbolo
  symbol->st_name = name_offset;
  symbol->st_value = value;
  symbol->st_size = size;
  symbol->st_info = info;
  symbol->st_other = other;
  symbol->st_shndx = shndx;
  //cáculo la posición de esta nueva entrada dentro de la tabla de símbolos
  *index = symbol_offset / sizeof(*symbol);
  return 0;
}

/*Guarda el símbolo en strtab (tabla de cadenas ELF) y la guarda en symtab(el buffer de símbolos).
Esta es función por comodidad, ya que hace 2 cosas:
- AÑdael el nombre de símbolos a strtab 
- AÑade la entrada a symtab

visualmente esta función agrupa
EJ: 
"main"
   ↓
xv6_tcc_put_elf_str()
   ↓
name_offset = 1 (el offset es 1 porque el primer caracter es \0)
   ↓
xv6_tcc_put_elf_sym_raw()
   ↓
entrada Elf64_Sym*/
int
xv6_tcc_put_elf_sym(struct Xv6TccElfBuffer *symtab,
                    struct Xv6TccElfStringTable *strtab,
                    uint64 value, uint64 size, int info, int other,
                    int shndx, const char *name, uint *index)
{
  uint name_offset = 0;

  //solo meto la cadena a la tabla de cadenas si el nombre no es nulo y no es una cadena vacía
  if(name && name[0]){
    if(xv6_tcc_put_elf_str(strtab, name, &name_offset) < 0)
      return -1;
  }

  //creo el símbolo y la meto a la tabla de símbolos
  return xv6_tcc_put_elf_sym_raw(symtab, name_offset, value, size,
                                 info, other, shndx, index);
}

/*Crea una relocación Elf64_Rela. Ósea, meto una entrada en un buffer de relocaciones, 
por ejemplo .rela.text*/
int
xv6_tcc_put_elf_rela(struct Xv6TccElfBuffer *rela,
                     uint64 offset, uint64 info, long addend)
{
  struct Xv6TccElfRela *entry;
  uint entry_offset;

  //reservo espacio en el buffer con alineación a 24 bytes por sizeof(Xv6TccElfRela)
  if(xv6_tcc_section_add(rela, sizeof(*entry),
                         sizeof(uint64), &entry_offset) < 0)
    return -1;


  /*COnvierto la dirección de la región reservada (la dirección donde van los datos) en un puntero a Xv6TccElfRela 
  para así poder meter los datos*/
  entry = (struct Xv6TccElfRela *)(rela->data + entry_offset);
  //meto los datos
  entry->r_offset = offset;
  entry->r_info = info;
  entry->r_addend = addend;
  return 0;
}
