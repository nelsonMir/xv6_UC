/*
xv6_tcc_object.h

Es una reprentación del fichero objeto a generar pero en memoria, todavía no genera el ELF 
en el disco.
Este fichero recibirá las líneas de código ya separadas por el parser de líneas 
y con ellas va construyendo las estructuras del ELF en memoria, todavía no se 
escriben en almacenamiento persistente.
En resumen es un estado educativo de un objeto ELF relocatable mientras se ensambla.
Mantiene el codigo de .text, los simbolos descubiertos y las relocaciones
pendientes. De momento todo se construye en memoria, luego me encargaré 
de escribir el fichero ELF64 ET_REL completo.
*/


#ifndef XV6_TCC_OBJECT_H
#define XV6_TCC_OBJECT_H

#include "user/tinycc/xv6_tcc_line.h"

#define XV6_TCC_OBJECT_MAX_SYMBOLS 32 //máximo número de símbolos que permito en un objeto
#define XV6_TCC_OBJECT_MAX_RELOCATIONS 64 //máximo número de relocaciones que permito en un objeto
#define XV6_TCC_OBJECT_MAX_CONSTANTS 32

//Scope/alcance del símbolo: local (solo válido en el fichero objeto actual) o global (visible por otros ficheros objeto durante el enlace)
#define XV6_TCC_STB_LOCAL 0
#define XV6_TCC_STB_GLOBAL 1
#define XV6_TCC_STB_WEAK 2
#define XV6_TCC_STT_NOTYPE 0

/* Estos indices coinciden con las secciones del ET_REL antiguo con menos secciones */
//Indica la sección en la que un símbolo está definido
#define XV6_TCC_SHN_UNDEF 0 //símbolo sin definir Ej: un salto a una función externa referencia "external_func". La hemos referenciado pero no definido
#define XV6_TCC_SHN_TEXT 1 //símbolo definiddo en la sección .text
#define XV6_TCC_SHN_RODATA 3
#define XV6_TCC_SHN_DATA 4
#define XV6_TCC_SHN_BSS 5

#define XV6_TCC_R_RISCV_32 1
#define XV6_TCC_R_RISCV_64 2
//tipo de inmediato a utilizar en una relocación/correción por parte del linker
#define XV6_TCC_R_RISCV_BRANCH 16
#define XV6_TCC_R_RISCV_JAL 17
#define XV6_TCC_R_RISCV_CALL 18
#define XV6_TCC_R_RISCV_PCREL_HI20 23
#define XV6_TCC_R_RISCV_PCREL_LO12_I 24

/*Esta estructura se utilizará para representar un símbolo mientras se trababja con él en memoria. 
UN símbolo puede ser:
- una etiqueta. EJ: "main:"
- Una referencia. EJ: "j main"*/
struct Xv6TccAssemblerSymbol {
  char name[XV6_TCC_LINE_NAME_MAX];
  uint64 value;
  uint64 size; //Es el tamaño del objeto asociado al síbmolo. EJ: función main --> tamaño de la función
  int section_index;
  int binding;
  int defined;
  uint elf_index;
};

/*Representa una correción o relocación que el linker deberá realizar posteriormente. 
EJ: beq a0, zero, done ------> como el ensamblador no escribe el desplazamiento verdadero, va a emitir provisionalmente un valor a 0 para "done" --> beq a0, zero, 0
Pero a su vez va a guardar la relocación pendiente:
offset = posición de la instrucción en .text
type = R_RISCV_BRANCH
symbol_slot = posición interna de done, su valor depende del orden en el que descubra los símbolos
addend = 0*/
struct Xv6TccAssemblerRelocation {
  int section_index;
  uint offset; //indica la posición dentro de la sección que debe corregirse
  uint type;
  int symbol_slot; //es su índice interno en object->symbols[]
  long addend;
};

struct Xv6TccAssemblerConstant {
  char name[XV6_TCC_LINE_NAME_MAX];
  long value;
};

/*Es el estado completo de un fichero objeto en memoria mientras se construye:
COntiene todos los búferes ELF: 
struct Xv6TccElfBuffer *text  -----> instrucciones generadas 
struct Xv6TccElfBuffer *symtab -----> tabla ELF final de símbolos
struct Xv6TccElfStringTable *strtab  ----> tabla de cadenas/nombres de símbolos
struct Xv6TccElfBuffer *rela_text   ------> tabla de relocaciones que afecten a .text

además hay otras estructuras
array symbols[32] es un representación sencilla que luego se convertirá en ".symtab". Es un array auxiliar en donde se meterán los símbolos antes de pasarlos a .symtab
array relocations[64] es una representación sencilla que luego se convertirá ".rela.text"
"first_global_symbol guarda el índice dentro de .symtab donde comienza los síḿbolos globales (ELF exige que los locales vayan primero)"
"finalized" indica si ya se construyeron las tablas finales del elf (1 = ya se construyó el objeto, 0 = todavía me faltan líneas por procesar)*/
struct Xv6TccObjectBuilder {
  struct Xv6TccElfBuffer *text;
  struct Xv6TccElfBuffer *rodata;
  struct Xv6TccElfBuffer *data;
  uint64 bss_size;

  struct Xv6TccElfBuffer *symtab;
  struct Xv6TccElfStringTable *strtab;
  struct Xv6TccElfBuffer *rela_text;
  struct Xv6TccElfBuffer *rela_rodata;
  struct Xv6TccElfBuffer *rela_data;

  /* Buffers vacios usados por la API de una versión antigua, ya no se usa*/
  struct Xv6TccElfBuffer fallback_rodata;
  struct Xv6TccElfBuffer fallback_data;
  uchar fallback_rodata_byte[1];
  uchar fallback_data_byte[1];
  struct Xv6TccElfBuffer fallback_rela_rodata;
  struct Xv6TccElfBuffer fallback_rela_data;
  uchar fallback_rela_rodata_byte[1];
  uchar fallback_rela_data_byte[1];

  int current_section;
  uint text_align;
  uint rodata_align;
  uint data_align;
  uint bss_align;

  struct Xv6TccAssemblerSymbol symbols[XV6_TCC_OBJECT_MAX_SYMBOLS];
  int symbol_count;

  struct Xv6TccAssemblerRelocation
      relocations[XV6_TCC_OBJECT_MAX_RELOCATIONS];
  int relocation_count;

  struct Xv6TccAssemblerConstant constants[XV6_TCC_OBJECT_MAX_CONSTANTS];
  int constant_count;

  uint first_global_symbol; //first_global_symbol guarda el índice dentro de .symtab donde comienza los síḿbolos globales
  int generated_symbol_count;
  int finalized;
};

/* API compatible con las etapas anteriores: crea .rodata/.data vacias. */
int xv6_tcc_object_init(struct Xv6TccObjectBuilder *object,
                        struct Xv6TccElfBuffer *text,
                        struct Xv6TccElfBuffer *symtab,
                        struct Xv6TccElfStringTable *strtab,
                        struct Xv6TccElfBuffer *rela_text);

/* API completa de implementación previa */
int xv6_tcc_object_init_sections(
    struct Xv6TccObjectBuilder *object,
    struct Xv6TccElfBuffer *text,
    struct Xv6TccElfBuffer *rodata,
    struct Xv6TccElfBuffer *data,
    struct Xv6TccElfBuffer *symtab,
    struct Xv6TccElfStringTable *strtab,
    struct Xv6TccElfBuffer *rela_text);


/* API completa de la última versión, con relocaciones de datos. */
int xv6_tcc_object_init_full(
    struct Xv6TccObjectBuilder *object,
    struct Xv6TccElfBuffer *text,
    struct Xv6TccElfBuffer *rodata,
    struct Xv6TccElfBuffer *data,
    struct Xv6TccElfBuffer *symtab,
    struct Xv6TccElfStringTable *strtab,
    struct Xv6TccElfBuffer *rela_text,
    struct Xv6TccElfBuffer *rela_rodata,
    struct Xv6TccElfBuffer *rela_data);

int xv6_tcc_object_process_line(struct Xv6TccObjectBuilder *object,
                                const struct Xv6TccParsedLine *line);

int xv6_tcc_object_finalize(struct Xv6TccObjectBuilder *object);

const struct Xv6TccAssemblerSymbol *
xv6_tcc_object_find_symbol(const struct Xv6TccObjectBuilder *object,
                           const char *name);

const struct Xv6TccAssemblerRelocation *
xv6_tcc_object_relocation_at(const struct Xv6TccObjectBuilder *object,
                             int index);

#endif
