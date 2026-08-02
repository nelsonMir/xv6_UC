/*
xv6_tcc_elf_reader.h

Lector y validador educativo de objetos ELF64 RISC-V ET_REL.
No copia las secciones a nuevos buffers: crea una vista con punteros hacia
las regiones validadas del fichero objeto cargado en memoria.
De momento lo único que haré será:
- abrir los ficheros objetos 
- validar su formato
- acceder a sus secciones
- leer sus símbolos
- leer sus relocaciones  
*/
#ifndef XV6_TCC_ELF_READER_H
#define XV6_TCC_ELF_READER_H

#include "user/tinycc/xv6_tcc_elf_writer.h"

#define XV6_TCC_READER_MAX_SECTIONS 64
#define XV6_TCC_READER_SECTION_NOT_FOUND -1 /*Como no nos podemos fiar que las secciones ELF aparezcan en el mismo orden (por si en un futuro cambio el orden en el que se meten en el ELF), utilizo esta constante
para indicar que aún no localizo la posición de la sección en el ELF*/

/*Es una estructura auxiliar para poder observar los campos del Fichero objeto ELF relocatable. 
A medidas vayamos validando los campos del ELF, se irán guardando las secciones o partes verificadas en campos auxiliares de este struct. 
AL inicio solo tendremos todo el fichero RAW objeto en el campo "data" pero a medida vayamos validando secciones EJ: el header del ELF, lo meteremos en el campo 
definitivo, en este caso "header" y así sucecivamente*/
struct Xv6TccRelObjectView {
  const uchar *data; //apunta al inicio del fichero objeto ELF relocatable que cargaremos en memoria al leerlo
  uint size; //tamaño de ese fichero objeto ELF relocatable

  const struct Xv6TccElfHeader *header; //apunta a la cabecera ELF del objeto
  const struct Xv6TccElfSectionHeader *sections; //Este struct representa el header de cada sección. Cada entrada será de 64 bytes

  /*Indices de las secciones en la tabla de nombres de secciones "shstrtab", por defecto XV6_TCC_READER_SECTION_NOT_FOUND*/
  int text_index;
  int rela_text_index;
  int symtab_index;
  int strtab_index;
  int shstrtab_index;

  //punteros a las cabeceras de sección de cada sección
  const struct Xv6TccElfSectionHeader *text_section;
  const struct Xv6TccElfSectionHeader *rela_text_section;
  const struct Xv6TccElfSectionHeader *symtab_section;
  const struct Xv6TccElfSectionHeader *strtab_section;
  const struct Xv6TccElfSectionHeader *shstrtab_section;

  //punteros al contenido de cada una de las secciones
  const uchar *text; //puntero al contenido de la sección .text (las instrucciones)
  const struct Xv6TccElfRela *relocations; //puntero al contenido de las relocaciones de .rela.text
  uint relocation_count; //var auxiliar del número de relocaciones, se calcula como sh_size / sizeof(Xv6TccElfRela)
  const struct Xv6TccElfSym *symbols; //puntero al array de símbolos 
  uint symbol_count; //var auxiliar del número de símbolos 
  const char *strtab; //puntero al array de cadenas
  uint strtab_size; //tamaño de la tabla de cadenas 
  const char *shstrtab; //puntero a la tabla de nombres de las secciones EJ: \0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0
  uint shstrtab_size; //tamaño de la tabla de nombres de las secciones
};

/*Analiza los bytes del objeto ya caragado en memoria. Devuelve el analisis del ELF relocatable en "view"*/
int xv6_tcc_parse_rel_object(
    const uchar *data, uint size,
    struct Xv6TccRelObjectView *view);

/*Carga un fichero objeto en disco en memoria a través de su nombre*/
int xv6_tcc_load_rel_object(
    const char *path,
    struct Xv6TccElfBuffer *storage,
    struct Xv6TccRelObjectView *view);

/*Devuelve el nombre de una sección en la tabla de nombres de secciones "shstrtab" dando su índice*/
const char *xv6_tcc_rel_section_name(
    const struct Xv6TccRelObjectView *view,
    int section_index);

/*Devuelve el índice el índice de la cabecera de una sección dado su nombre*/
int xv6_tcc_rel_find_section(
    const struct Xv6TccRelObjectView *view,
    const char *name);

/*Devuelve un símbolo de la tabla de símbolos ".symtab" dado su índice en la tabla*/
const struct Xv6TccElfSym *xv6_tcc_rel_symbol_at(
    const struct Xv6TccRelObjectView *view,
    uint index);

/*Obtiene el nombre de un símbolo*/
const char *xv6_tcc_rel_symbol_name(
    const struct Xv6TccRelObjectView *view,
    const struct Xv6TccElfSym *symbol);

/*Accede a una relocación de .rela.text dando el índice en la tabla de relocaciones*/
const struct Xv6TccElfRela *xv6_tcc_rel_relocation_at(
    const struct Xv6TccRelObjectView *view,
    uint index);

#endif
