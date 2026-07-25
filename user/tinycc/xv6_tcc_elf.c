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
 
  Licencia: GNU LGPL 2.1 o posterior. 
 */

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf.h"

/*EL módulo ELF de tinyCC se usará tanto para el assembler como el linker*/

/*Reondea una posición hasta que sea múltipo de una alineación múltiplo de 2*/
static uint
align_up(uint value, uint align)
{
  if(align <= 1)
    return value;

  //esta función sirve cuando "align" es múltiplo de 2
  return (value + align - 1) & ~(align - 1);
}

/*Va llenando el buffer de elementos de una sección (no guarda los elementos ahí, el buffer
solo lleva en cuenta el número de elementos que tiene), para ello:
  - el buffer de la sección
  - se manda el nḿero de bytes a agregar
  - alineación requerida 
  - el offset 

  Está función hace lo sguiente en este orden:
  - cálcula donde van a comenzar a meterse los nuevos elementos respetando la alineación (hace padding)
  - Reserva lógicamente esos bytes con el buffer con section->size
  - La región reservada dónde se meterán los neuvos elementos los inicializa a cero
  */
int
xv6_tcc_section_add(struct Xv6TccElfBuffer *section,
                    uint bytes, uint align, uint *offset)
{
  uint start;
  uint end;

  if(!section || !section->data || !offset)
    return -1;
  if(align == 0 || (align & (align - 1)) != 0)
    return -1;

  start = align_up(section->size, align);
  if(start > section->capacity || bytes > section->capacity - start)
    return -1;

  end = start + bytes;
  if(start > section->size)
    memset(section->data + section->size, 0, start - section->size);
  if(bytes)
    memset(section->data + start, 0, bytes);

  section->size = end;
  *offset = start;
  return 0;
}

/*Basada en put_elf_str() de TInyCC
Añade una cadena a la tabla de cadenas ELF "strab",
cada cadena inicia con \0

parámetors:
- la tabla de cadenas
- la cadena
- el offset: es el valor que devolverá que indica en donde comienza la cadena agregada en la tabla 

EJ: posición: 0 1 2 3 4 5
  contenido: \0 m a i n \0
  
  Agrego "message"
  entonces quedaría 
  posición: 0 1 2 3 4 5 6 7 8 9 10 11 12 13
contenido: \0 m a i n \0 m e s s  a  g  e \0
y el offset = 6*/
int
xv6_tcc_put_elf_str(struct Xv6TccElfStringTable *table,
                    const char *text, uint *offset)
{
  uint length;

  if(!table || !table->data || !text || !offset)
    return -1;

  length = strlen(text) + 1;
  if(table->size > table->capacity ||
     length > table->capacity - table->size)
    return -1;

  *offset = table->size;
  memmove(table->data + table->size, text, length);
  table->size += length;
  return 0;
}

/*Guarda un símbolo en el buffer de símbolos "symtab" cuando ya se ha guardado 
el offset en la tabla de cadenas ELF srtab*/
int
xv6_tcc_put_elf_sym_raw(struct Xv6TccElfBuffer *symtab,
                        uint name_offset, uint64 value, uint64 size,
                        int info, int other, int shndx, uint *index)
{
  struct Xv6TccElfSym *symbol;
  uint symbol_offset;

  if(!symtab || !index)
    return -1;

  if(xv6_tcc_section_add(symtab, sizeof(*symbol),
                         sizeof(uint64), &symbol_offset) < 0)
    return -1;

  symbol = (struct Xv6TccElfSym *)(symtab->data + symbol_offset);
  symbol->st_name = name_offset;
  symbol->st_value = value;
  symbol->st_size = size;
  symbol->st_info = info;
  symbol->st_other = other;
  symbol->st_shndx = shndx;
  *index = symbol_offset / sizeof(*symbol);
  return 0;
}

/*Guarda el símbolo en strtab (tabla de cadenas ELF) y la guarda en symtab(el buffer de símbolos)

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

  if(name && name[0]){
    if(xv6_tcc_put_elf_str(strtab, name, &name_offset) < 0)
      return -1;
  }

  return xv6_tcc_put_elf_sym_raw(symtab, name_offset, value, size,
                                 info, other, shndx, index);
}

/*Crea una relocación Elf64_Rela, ósea un símbolo del código que todavía 
no puede calcularse su dirección, cualdo el linker la conozca, deberá corregirla*/
int
xv6_tcc_put_elf_rela(struct Xv6TccElfBuffer *rela,
                     uint64 offset, uint64 info, long addend)
{
  struct Xv6TccElfRela *entry;
  uint entry_offset;

  if(xv6_tcc_section_add(rela, sizeof(*entry),
                         sizeof(uint64), &entry_offset) < 0)
    return -1;

  entry = (struct Xv6TccElfRela *)(rela->data + entry_offset);
  entry->r_offset = offset;
  entry->r_info = info;
  entry->r_addend = addend;
  return 0;
}
