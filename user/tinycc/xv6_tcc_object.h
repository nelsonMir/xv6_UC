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

//Scope/alcance del símbolo: local (solo válido en el fichero objeto actual) o global (visible por otros ficheros objeto durante el enlace)
#define XV6_TCC_STB_LOCAL 0
#define XV6_TCC_STB_GLOBAL 1
#define XV6_TCC_STT_NOTYPE 0

//Indica la sección en la que un símbolo está definido
#define XV6_TCC_SHN_UNDEF 0 //símbolo sin definir Ej: un salto a una función externa referencia "external_func". La hemos referenciado pero no definido
#define XV6_TCC_SHN_TEXT 1 //símbolo definiddo en la sección .text

//tipo de inmediato a utilizar en una relocación/correción por parte del linker
#define XV6_TCC_R_RISCV_BRANCH 16
#define XV6_TCC_R_RISCV_JAL 17

/*Esta estructura se utilizará para representar un símbolo mientras se trababja con él en memoria. 
UN símbolo puede ser:
- una etiqueta. EJ: "main:"
- Una referencia. EJ: "j main"*/
struct Xv6TccAssemblerSymbol {
  char name[XV6_TCC_LINE_NAME_MAX]; //guarda el nombre del símbolo
  uint64 value; /*guarda el desplazamiento del símbolo dentro de su sección. EJ: 
                  addi a0, zero, 1  # bytes 0-3
                  beq a0, zero, done # bytes 4-7
                  j external_func    # bytes 8-11

              done:
              CUando aparece donde en la sección .text, esta sección ya tiene 12 bytes por lo que "done.vale = 12"
              OJO esto todavía no es una dirección absoluta, es un offset dentro de .text*/
  uint64 size; //Es el tamaño del objeto asociado al síbmolo. EJ: función main --> tamaño de la función
  int section_index; //indica la sección en la que está definido el símbolo, hay 2 valores de momento: XV6_TCC_SHN_UNDEF y XV6_TCC_SHN_TEXT
  int binding; /*indica si el símbolo es local o global, sus valores pueden ser XV6_TCC_STB_LOCAL, XV6_TCC_STB_GLOBAL
                - local: solo puede utilizarse dentro del mismo fichero objeto
                - global: puede verse desde otros ficheros objeto durante el enlace*/
  int defined; /*Indica si ya apareció la definición de un símbolo o solo ha sido declarado o referenciado:
                EJ: al encontrar el símbolo done por primera vez:
                    beq a0, zero, done
                    Se marca como declarado o referenciado ---> defined = 0
                    
                    SI luego aparece:
                    done:
                    entonces ---> defined = 1, vale = posición actual de .text*/
  uint elf_index; /*Es el índice definitivo del símbolo dentro de la tabla de símbolos "symtab". NO se pone al crear el símbolo porque ELF exige ordenar:
                  símbolo nulo, símbolos locales, símbolos globales. Así que esta asignación se hace en la función xv6_tcc_object_finalize.
                  En Xv6TccAssemblerRelocation al detectar un símbolo por orden de aparición su valor de spone en "symbol_slot" pero ese no será necesariamente el 
                  orden, poorque un fichero ELF pone los símbolos locales antes que los globales EJ:
                  índice ELF 0 → símbolo nulo
                  índice ELF 1 → done, local
                  índice ELF 2 → main, global
                  índice ELF 3 → external_func, global*/
};

/*Representa una correción o relocación que el linker deberá realizar posteriormente. 
EJ: beq a0, zero, done ------> como el ensamblador no escribe el desplazamiento verdadero, va a emitir provisionalmente un valor a 0 para "done" --> beq a0, zero, 0
Pero a su vez va a guardar la relocación pendiente:
offset = posición de la instrucción en .text
type = R_RISCV_BRANCH
symbol_slot = posición interna de done, su valor depende del orden en el que descubra los símbolos
addend = 0*/
struct Xv6TccAssemblerRelocation {
  uint offset; //indica la posición dentro de la sección que debe corregirse
  uint type; /*INdica el tipo de correción que debe hacer el linker (el tipo del inmediato a usar B o J): XV6_TCC_R_RISCV_BRANCH o XV6_TCC_R_RISCV_JAL*/
  int symbol_slot; //es su índice interno en object->symbols[]
  long addend; /*es el valor adicional que participa en el cálculo de la relocación de momento siempre será 0 pero 
  si me queda tiempo implementaré expresiones como j simbolo+4*/
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
  struct Xv6TccElfBuffer *symtab;
  struct Xv6TccElfStringTable *strtab;
  struct Xv6TccElfBuffer *rela_text;

  struct Xv6TccAssemblerSymbol symbols[XV6_TCC_OBJECT_MAX_SYMBOLS];
  int symbol_count;

  struct Xv6TccAssemblerRelocation relocations[XV6_TCC_OBJECT_MAX_RELOCATIONS]; //array auxiliar donde meteré las relocaciones del objeto
  int relocation_count;

  uint first_global_symbol; //first_global_symbol guarda el índice dentro de .symtab donde comienza los síḿbolos globales
  int finalized;
};

int xv6_tcc_object_init(struct Xv6TccObjectBuilder *object,
                        struct Xv6TccElfBuffer *text,
                        struct Xv6TccElfBuffer *symtab,
                        struct Xv6TccElfStringTable *strtab,
                        struct Xv6TccElfBuffer *rela_text);

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
