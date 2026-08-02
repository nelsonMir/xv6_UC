/*
xv6_tcc_elf_writer.h

Construccion y escritura de un fichero objeto ELF64 RISC-V ET_REL.
Recibe los buffers ya finalizados por Xv6TccObjectBuilder y los empaqueta con
una cabecera ELF, cabeceras de seccion y una tabla .shstrtab.
COnstruye el ELF raw en memoria con un buffer llamado "image" y luego hace la escritura en disco
*/
#ifndef XV6_TCC_ELF_WRITER_H
#define XV6_TCC_ELF_WRITER_H

#include "user/tinycc/xv6_tcc_object.h"

//Valores de la cabecera ELF ET_REL (relocatable)
#define XV6_TCC_ELF_NIDENT 16 //cabecera ELF comienza con 16 bytes llamados "e_ident"

#define XV6_TCC_ET_REL 1 //tipo de fichero ---> ET_REL (fichero relocatable ---> es un .o, no un ejecutable)
#define XV6_TCC_EM_RISCV 243 //indicamos la arquitectura --> decimos que es código de RISC-V
#define XV6_TCC_EV_CURRENT 1 //Versión actual del formato ELF

#define XV6_TCC_ELFCLASS64 2 //Clase del ELF ---> ELF de 64 bits (acepta direcciones de 64 bits)
#define XV6_TCC_ELFDATA2LSB 1 //Endianness --> little-endian

//tipos de sección
#define XV6_TCC_SHT_NULL 0 //sección nula obligatoria en el índice 0 del fichero objeto
#define XV6_TCC_SHT_PROGBITS 1 //contiene bytes definidos en el programa ---> se usa para indicar la sección .text
#define XV6_TCC_SHT_SYMTAB 2 //tabla de símbolos
#define XV6_TCC_SHT_STRTAB 3 //tabla de strings (tanto .strtab como .shstrtab)
#define XV6_TCC_SHT_RELA 4 //tabla de relocaciones con addend explícito ---> .rela.text

//flags de sección: para .text se combinan ambos  ---> aunque el .o todavía no se carga, estas flags le indicarán al linker que es un .text
#define XV6_TCC_SHF_ALLOC 2 //indica que la sección debera ocupar memoria cuando forme parte de un ejecutable
#define XV6_TCC_SHF_EXECINSTR 4 //indica que la sección contiene instrucciones ejecutables

/*índices de secciones: 
0 NULL
1 .text
2 .rela.text
3 .symtab
4 .strtab
5 .shstrtab*/
#define XV6_TCC_REL_SECTION_NULL 0
#define XV6_TCC_REL_SECTION_TEXT 1
#define XV6_TCC_REL_SECTION_RELA_TEXT 2
#define XV6_TCC_REL_SECTION_SYMTAB 3
#define XV6_TCC_REL_SECTION_STRTAB 4
#define XV6_TCC_REL_SECTION_SHSTRTAB 5
#define XV6_TCC_REL_SECTION_COUNT 6 

/*Este struct representará el header del ELF, SON 64 BYTES*/
struct Xv6TccElfHeader {
  uchar e_ident[XV6_TCC_ELF_NIDENT]; //identifica el fichero como ELF64 Little-endian
  ushort e_type; //ET_REL ---> fichero elf relocatable
  ushort e_machine; //e_machine = RISC-V
  uint e_version; //Versión del actual formato ELF
  uint64 e_entry; //dirección de entrada. Como es un .o entonces e_entry = 0, no es ejecutable todavía
  uint64 e_phoff; //e_phoff = 0, no hay segmentos cargables 
  uint64 e_shoff; // Offset dentro del fichero donde comienza la tabla de cabeceras de la sección
  uint e_flags; //flags específicas de RISC-V, de momento cero
  ushort e_ehsize; //tamaño de la cabecera ELF -> 64 bytes
  ushort e_phentsize; //cabecera del programa = 0
  ushort e_phnum; //otra cabecera del programa = 0
  ushort e_shentsize; //tamaño de la cabecera de cada sección = 64 (definida abajo)
  ushort e_shnum; //Número de secciones del ELF = 6 (null, .text, .rela.text, .symtab, .strtab, .shstrtab)
  ushort e_shstrndx; //índice de la sección que contiene los nombres de las secciones (5 ---> shstrtab, aunque ese número puede variar, pero de momento así lo esoty construyendo)
};

/*Este struct representa el header de cada sección. Cada entrada será de 64 bytes*/
struct Xv6TccElfSectionHeader {
  uint sh_name; //no guardar el nombre de la sección EJ .text ---> sino que guarda el offset del nombre en la tabla de secciones .shstrtab
  uint sh_type; //INdica el tipo de la sección (PROGBITS, RELA, SYMTAB, STRTAB)
  uint64 sh_flags; //características de la sección
  uint64 sh_addr; //dirección virtual de la sección. COmo es un ET_REL entonces = 0, @s finales las decidirá el linker
  uint64 sh_offset; //offset físico dentro del fichero objeto EJ .text empieza en el byte 64
  uint64 sh_size; //num bytes de la sección
  uint sh_link; //Relación con otra sección EJ: .rela.text.sh_link = índice de .symtab
  uint sh_info; //información adicional cuyo significado depende del tipo EJ: .rela.text.sh_link = índice de .symtab
  uint64 sh_addralign; //alineación requerida
  uint64 sh_entsize; //tamaño de cada entrada cuando la sección es una tabla. EJ: .symtab --> 24 bytes por símbolo, .rela.text ---> 24 bytes por relocación 
};

int xv6_tcc_build_rel_elf(
    const struct Xv6TccObjectBuilder *object,
    struct Xv6TccElfBuffer *image,
    struct Xv6TccElfStringTable *shstrtab);

int xv6_tcc_write_elf_file(const char *path,
                            const struct Xv6TccElfBuffer *image);

int xv6_tcc_write_rel_object(
    const struct Xv6TccObjectBuilder *object,
    const char *path,
    struct Xv6TccElfBuffer *image,
    struct Xv6TccElfStringTable *shstrtab);

#endif
