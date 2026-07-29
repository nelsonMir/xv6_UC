/*
xv6_tcc_elf_writer.c

Escritor educativo de objetos ELF64 RISC-V ET_REL.
Genera el ELF raw en memoria con un buffer llamado "image" y luego lo escribe en disco.
La organizacion de cabeceras y secciones sigue el modelo de salida ELF de
TinyCC tccelf.c, reducido a .text, .rela.text, .symtab, .strtab y .shstrtab.

Donante conceptual:
  TinyCC tccelf.c


*/

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_writer.h"

/*Es un wrapper para llamar a la función que inserta nombres en la tabla de nombres de secciones .shstrtab*/
static int
put_section_name(struct Xv6TccElfStringTable *table,
                 const char *name, uint *offset)
{
  return xv6_tcc_put_elf_str(table, name, offset);
}

/*Copia los bytes de una sección completa dentro de una imagen ELF.
La imagen ELF tenrá la secuencia de bytes en crudo que formarán el .o
*/
static int
copy_buffer(struct Xv6TccElfBuffer *image,
            const void *source, uint bytes, uint align,
            uint *offset)
{
  if(!image || !source || !offset)
    return -1;

  /*Reservo el espacio dentro del buffer de datos de la imagen
  igual al tamaño de la sección que voy a meter. Recuerda que esa función 
  también inicializa a cero los bytes en donde meteré cosas*/
  if(xv6_tcc_section_add(image, bytes, align, offset) < 0)
    return -1;

  /*COpio el contenido real de la sección en el buffer de datos de la imagen*/
  if(bytes)
    memmove(image->data + *offset, source, bytes);
  return 0;
}

/*Rellena los primeros bytes de la cabecera ELF. 
Primero mete la firma ELF:
header->e_ident[0] = 0x7f;
header->e_ident[1] = 'E';
header->e_ident[2] = 'L';
header->e_ident[3] = 'F';
Luego indico lo siguiente 
e_ident[4] = 4; --> ELF64
e_ident[5] = 1; --> little-endian
e_ident[6] = 1; --> versión actual
*/
static void
fill_elf_ident(struct Xv6TccElfHeader *header)
{
  header->e_ident[0] = 0x7f;
  header->e_ident[1] = 'E';
  header->e_ident[2] = 'L';
  header->e_ident[3] = 'F';
  header->e_ident[4] = XV6_TCC_ELFCLASS64;
  header->e_ident[5] = XV6_TCC_ELFDATA2LSB;
  header->e_ident[6] = XV6_TCC_EV_CURRENT;
}

/*Contruye la imagen completa del ELF (la secuencia de bytes) en memoria en el buffer "Image" pero todavía no la
escribe en disco. Diferencia entre el buffer object e Image: Object contiene toda la información del objeto pero dividia lógicamente: 
punteros a los búferes, estado de finalización, relocaciones internas, símbolos internos. En cambio en imagen tengo la secuencia de bytes 
en crudo a escribir en el fichero objeto .o directo*/
int
xv6_tcc_build_rel_elf(const struct Xv6TccObjectBuilder *object,
                       struct Xv6TccElfBuffer *image,
                       struct Xv6TccElfStringTable *shstrtab)
{
  struct Xv6TccElfHeader *header; //puntero auxiliar para meter los datos en el header elf
  struct Xv6TccElfSectionHeader *sections; /*puntero auxiliar para meter los datos en la tabla de header de secciones. 
  IMPORTANTE: cada entrada de la tabla de cabeceras de sección será un Xv6TccElfSectionHeader y como tengo 6 secciones, 
  tendrè 6 cabeceras*/
  uint section_names[XV6_TCC_REL_SECTION_COUNT]; //aquí guardaré los offsets de cada nombre de cada seccción en la tabla de nombres de secciones .shstrtab
  uint header_offset; //var donde se guardará el offset del header ELF (vadrá cero 0)
  uint text_offset;
  uint rela_text_offset;
  uint symtab_offset;
  uint strtab_offset;
  uint shstrtab_offset;
  uint section_headers_offset; //var dónde se guardará el offset hasta la tabla de header de secciones (bytes header elf + bytes todas las demás secciones)

  if(!object || !object->finalized ||
     !object->text || !object->symtab || !object->strtab ||
     !object->rela_text || !image || !image->data ||
     !shstrtab || !shstrtab->data)
    return -1;

  /*Hay que comprobar los tamaños porque ELF exige tamaños exactos */
  if(sizeof(struct Xv6TccElfHeader) != 64 ||
     sizeof(struct Xv6TccElfSectionHeader) != 64 ||
     sizeof(struct Xv6TccElfSym) != 24 ||
     sizeof(struct Xv6TccElfRela) != 24)
    return -1;

  /*Vaciar la imagen y .shstrtab ---> esto no borra la memoria, solo indicamos que está vacío el buffer (ósea que si 
  había información antes, pues no es válida)*/
  image->size = 0;
  shstrtab->size = 0;

  /*Se construye la tabla de secciones:
  \0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0
  
  Y se guardan los offsets de cada nombre en section_names: 
  section_names[0] = 0
  section_names[1] = 1
  section_names[2] = 7
  section_names[3] = 18
  section_names[4] = 26
  section_names[5] = 34*/
  if(put_section_name(shstrtab, "", &section_names[0]) < 0 ||
     section_names[0] != 0 ||
     put_section_name(shstrtab, ".text", &section_names[1]) < 0 ||
     put_section_name(shstrtab, ".rela.text", &section_names[2]) < 0 ||
     put_section_name(shstrtab, ".symtab", &section_names[3]) < 0 ||
     put_section_name(shstrtab, ".strtab", &section_names[4]) < 0 ||
     put_section_name(shstrtab, ".shstrtab", &section_names[5]) < 0)
    return -1;

  /*Se reserva espacio para la cabecera ELF dentro del buffer de la iamgen
  en "header_offseet = 0", ya que me devuelve el valor donde inica lo reservado.*/
  if(xv6_tcc_section_add(image, sizeof(struct Xv6TccElfHeader),
                         sizeof(uint64), &header_offset) < 0 ||
     header_offset != 0)
    return -1;

  /*Se copian el contenido de todas las secciones del buffer del fichero objeto (con sus datos) en la imagen del ELF en el siguiente orden:
    .text
    .rela.text
    .symtab
    .strtab
    .shstrtab*/
  if(copy_buffer(image, object->text->data, object->text->size,
                 4, &text_offset) < 0 ||
     copy_buffer(image, object->rela_text->data, object->rela_text->size,
                 sizeof(uint64), &rela_text_offset) < 0 ||
     copy_buffer(image, object->symtab->data, object->symtab->size,
                 sizeof(uint64), &symtab_offset) < 0 ||
     copy_buffer(image, object->strtab->data, object->strtab->size,
                 1, &strtab_offset) < 0 ||
     copy_buffer(image, shstrtab->data, shstrtab->size,
                 1, &shstrtab_offset) < 0)
    return -1;

  /*En el paso anterior copiamos el contenido de todas las secciones en la "imagen", ahora reservamos espacio 
  para meter la tabla de las cabeceras de seccción para saber dónde inicia cada sección. Esta cabecerá se pone después
  de la tabla .shstrtab, aunque en realidad se puede poner en cualquier parte del ELF, ya que el offset de su ubicación 
  lo indicamos con "header->e_shoff".
  "section_header_offset = (bytes cabecera elf + bytes de todas las secciones)".
  IMPORTANTE: cada entrada de la tabla de cabeceras de sección será un Xv6TccElfSectionHeader y como tengo 6 secciones, 
  tendrè 6 cabeceras*/
  if(xv6_tcc_section_add(
         image,
         XV6_TCC_REL_SECTION_COUNT *
             sizeof(struct Xv6TccElfSectionHeader),
         sizeof(uint64), &section_headers_offset) < 0)
    return -1;

 /*Se procederá a meter los datos de la Cabecera ELF*/


  //puntero al header ELF, y lo convierto en formato de estructura Xv6TccElfHeader para acceder a sus campos y llenar la cabecera
  header = (struct Xv6TccElfHeader *)(image->data + header_offset);
  //puntero a la cabecera de la tabla de secciones y lo convierto en formato de estructura Xv6TccElfSectionHeader y llenar el array de cada entrada
  sections = (struct Xv6TccElfSectionHeader *)(
      image->data + section_headers_offset);

  //limpio todos los bytes de la cabecera ELF poniendo 0's
  memset(header, 0, sizeof(*header));

  //limpio todos los bytes de la cabecera de la tabla de secciones. Como esta tabla tiene 6 entradas, pongo todas a 0's
  memset(sections, 0, XV6_TCC_REL_SECTION_COUNT * sizeof(*sections));

  /*relleno la cabecera ELF
  Los únicos campos que no asigno aquí son: 
  e_entry
  e_phoff
  e_phnum
  
  porque al ser un .o no tiene ni punto de entrada ni progran header. Se mantienen con valor 9*/
  fill_elf_ident(header);
  header->e_type = XV6_TCC_ET_REL;
  header->e_machine = XV6_TCC_EM_RISCV;
  header->e_version = XV6_TCC_EV_CURRENT;
  header->e_shoff = section_headers_offset;
  header->e_ehsize = sizeof(*header);
  header->e_shentsize = sizeof(*sections);
  header->e_shnum = XV6_TCC_REL_SECTION_COUNT;
  header->e_shstrndx = XV6_TCC_REL_SECTION_SHSTRTAB;

  /*Inicializo el header de la sección .text en la tabla de headers de secciones*/
  sections[XV6_TCC_REL_SECTION_TEXT].sh_name = section_names[1]; //de ahí saco el nombre de la sección
  sections[XV6_TCC_REL_SECTION_TEXT].sh_type = XV6_TCC_SHT_PROGBITS;
  sections[XV6_TCC_REL_SECTION_TEXT].sh_flags =
      XV6_TCC_SHF_ALLOC | XV6_TCC_SHF_EXECINSTR;
  sections[XV6_TCC_REL_SECTION_TEXT].sh_offset = text_offset;
  sections[XV6_TCC_REL_SECTION_TEXT].sh_size = object->text->size;
  sections[XV6_TCC_REL_SECTION_TEXT].sh_addralign = 4;

  //inicializo el header de .rela.text (relocaciones de .text)
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_name = section_names[2];
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_type = XV6_TCC_SHT_RELA;
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_offset = rela_text_offset;
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_size = object->rela_text->size;
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_link = XV6_TCC_REL_SECTION_SYMTAB; //las relocaciones utilizan la tabla de símbolos de la sección 3 (.symtab)
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_info = XV6_TCC_REL_SECTION_TEXT;
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_addralign =
      sizeof(uint64);
  sections[XV6_TCC_REL_SECTION_RELA_TEXT].sh_entsize =
      sizeof(struct Xv6TccElfRela);

  //inicializo el header de la  sección .symtab
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_name = section_names[3];
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_type = XV6_TCC_SHT_SYMTAB;
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_offset = symtab_offset;
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_size = object->symtab->size;
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_link =
      XV6_TCC_REL_SECTION_STRTAB;
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_info =
      object->first_global_symbol;
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_addralign = sizeof(uint64);
  sections[XV6_TCC_REL_SECTION_SYMTAB].sh_entsize =
      sizeof(struct Xv6TccElfSym);

  //inicializo el header de la  sección .strtab
  sections[XV6_TCC_REL_SECTION_STRTAB].sh_name = section_names[4];
  sections[XV6_TCC_REL_SECTION_STRTAB].sh_type = XV6_TCC_SHT_STRTAB;
  sections[XV6_TCC_REL_SECTION_STRTAB].sh_offset = strtab_offset;
  sections[XV6_TCC_REL_SECTION_STRTAB].sh_size = object->strtab->size;
  sections[XV6_TCC_REL_SECTION_STRTAB].sh_addralign = 1;

  //inicializo el header de la  sección .shstrtab
  sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_name = section_names[5];
  sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_type = XV6_TCC_SHT_STRTAB;
  sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_offset = shstrtab_offset;
  sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_size = shstrtab->size;
  sections[XV6_TCC_REL_SECTION_SHSTRTAB].sh_addralign = 1;

  return 0;
}

/*En image->data está el contenido del ELF como secuenica de bytes. Así que esta función 
escribirá esos datos en disco*/
int
xv6_tcc_write_elf_file(const char *path,
                        const struct Xv6TccElfBuffer *image)
{
  int file;
  int written;

  if(!path || !image || !image->data)
    return -1;

  //se elimina un fichero que tenga el mismo nombre
  unlink(path);

  //se crea el nuevo fichero
  file = open(path, O_CREATE | O_WRONLY);
  if(file < 0)
    return -1;

  //num bytes escritos
  written = 0;

  //bucle de escritura
  while(written < (int)image->size){
    int amount;

    amount = write(file, image->data + written,
                   image->size - written);
    if(amount <= 0){
      close(file);
      return -1;
    }
    written += amount;
  }

  if(close(file) < 0)
    return -1;
  return 0;
}

/*Esta función es por comodidad, hace ambas cosas:
1. Escribe la imagen ELF en memoria 
2. Luego escribe esa imagen en disco*/
int
xv6_tcc_write_rel_object(const struct Xv6TccObjectBuilder *object,
                          const char *path,
                          struct Xv6TccElfBuffer *image,
                          struct Xv6TccElfStringTable *shstrtab)
{
  if(xv6_tcc_build_rel_elf(object, image, shstrtab) < 0)
    return -1;
  return xv6_tcc_write_elf_file(path, image);
}
