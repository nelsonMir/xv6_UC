/*
  xv6_tcc_elf.h
 
  Secciones y helpers ELF reducidos a partir de tccelf.c de TinyCC.
  FUnción original: https://raw.githubusercontent.com/TinyCC/tinycc/d9d02c56401e43be43760b63f7d82f771a7ed1f6/tccelf.c
 */
#ifndef XV6_TCC_ELF_H
#define XV6_TCC_ELF_H

/*ALmacena bloques de datos de una sección. Esta no es una sección ELF completa (falta más información que se agregará en la cabeceras ELF, aquí solo se controlan bytes), 
es solamente el almacenamiento en memoria para ir construyendo el contenido de una sección
  
TInyCC usa arrays dinámicos, es decir, va amplicando la memoria automáticamente. COmo en esta versión de xv6 
aún no he creado la función realloc() para que las secciones puedan crecer dinámicamente cuando se necesitan más espacio pues traabajo
con tamaños fijos*/
struct Xv6TccElfBuffer {
  uchar *data; //APUnta a la memorial real donde se almacena los bytes
  uint size; //número de bytes que ya  pertenecen a la sección (se va llenando porque voy metiendo elementos )
  uint capacity; //capactiy: número de bytes que caben en la sección completa reservada de "data"
};

/*Tabla de cadenas ELF "strab". también vale para "shstrtab" ya que ambas tienen el mismo formato, son secuencias de carácteres terminadas por \0

UN archivo ELF necesita guardar eitquetas EJ: _Start, main...

Esos nombres no se almacenan en la cabecera ELF, sino que se guardan todos juntos en la tabla de cadenas ELF así:
\0main\0mensaje\0_start\0*/
struct Xv6TccElfStringTable {
  char *data; //APUnta a la memorial real donde se almacena los bytes
  uint size;
  uint capacity;
};

/*Representa una entrada ELF64 de la tabla de símbolos "symtab"

Cada una de estas entradas sirve para identificar al símbolo o cadena en la tabla de cadenas ELF. Cada entrada tendrá su propio struct. NO es una tabla. 
La tabla de símbolos es del tipo Xv6TccElfBuffer

Esta estructura reproduce el orden de campos de Elf64_Sym en su struct:
st_name       4 bytes  contiene el offset dentro de .strtab   EJ:offset 0 -> \0, offset 1 -> main\0, offset 6 -> mensaje\0
                       con el offset ya podría recuperar el nombre del símbolo de la tabla se símbolos: char *name = strtab.data + symbol->st_name;
st_info       1 byte   EMpaqueta dos cosas en un solo byte: bits 7..4 -> binding y bits 3..0 -> type. EL binding indica el alcance del símbolo: 
                       STB_LOCAL   -> símbolo local al objeto, STB_GLOBAL  -> símbolo visible para otros objetos, STB_WEAK    -> símbolo global.
                       EL type representa qué es ese símbolo: STT_NOTYPE  -> no especificado, STT_OBJECT  -> dato o variable,STT_FUNC    -> función o código ejecutable,
                       STT_SECTION -> sección
st_other      1 byte   SE utiliza para la visibilidad. EN mi caso usaré =0 siempre para visibilidad por defecto
st_shndx      2 bytes  INdica el índice de la sección en la que está definido el símbolo, ósea en qué sección fue definido Ej: un función ubicada en .text tendría st_shndx = 1
st_value      8 bytes
st_size       8 bytes  INdica cuántos bytes ocupa el símbolo. Ej función main de 20 bytes -> st_size = 20, array de 32 bytes -> st_size = 32
              --------
total        24 bytes
 */
struct Xv6TccElfSym {
  uint st_name; //no guarda el nombre del símbolo/cadena sino que el offset de éste en la tabla de cadenas ELF
  //se guarda el offset y no punteros porque un fichero elf está en disco y no en memoria
  uchar st_info;
  uchar st_other;
  ushort st_shndx;
  uint64 st_value;
  uint64 st_size;
};

/*Reprenta un relocación ELF64 con addend explícito (ósea que puede tener un valor adicional que puede particiar en el cálculo de la direción): Una relocación es una instrucción 
para el linker que le indica que en ese lugar en concreto del código o de los datos hay un valor que debe recalcularse*/
struct Xv6TccElfRela { //la siguiente explicación es válida para un objeto relocatable
  uint64 r_offset; //el offset de la sección donde debe hacerse la correción. EJ: si la corección es de .text (.rela.text), si r_offset = 12 entonces la correción afecta a los bytes 
  //que comienzan en el offset 12 de la sección .text
  uint64 r_info; //el símbolo + tipo de corrección: 32 bits superiores el índice del símbolo y 32 bits inferiores el tipo de reloación
  long r_addend; //valor adicional que puede particiar en el cálculo de la direción. EJ: .dword mensaje + 8 --> r_addend = 8
};

int xv6_tcc_section_add(struct Xv6TccElfBuffer *section,
                        uint bytes, uint align, uint *offset);
int xv6_tcc_put_elf_str(struct Xv6TccElfStringTable *table,
                        const char *text, uint *offset);
int xv6_tcc_put_elf_sym_raw(struct Xv6TccElfBuffer *symtab,
                            uint name_offset, uint64 value, uint64 size,
                            int info, int other, int shndx, uint *index);
int xv6_tcc_put_elf_sym(struct Xv6TccElfBuffer *symtab,
                        struct Xv6TccElfStringTable *strtab,
                        uint64 value, uint64 size, int info, int other,
                        int shndx, const char *name, uint *index);
int xv6_tcc_put_elf_rela(struct Xv6TccElfBuffer *rela,
                         uint64 offset, uint64 info, long addend);

#endif
