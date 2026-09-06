/*
xv6_tcc_as_core.c

Coordinador del ensamblador educativo.
Lee un fichero fuente, analiza cada linea, construye simbolos y relocaciones
en memoria y escribe un objeto ELF64 RISC-V ET_REL.

A través de este fichero controlaré todas las demás funcionalidades de los demás ficheros/módulos
para generar el ELF relocatable, para ello realizaré los siguientes pasos:
1. Se leerá el fichero en ensamblador .s
2. Se dividirá el líneas
3. Con el parser de líneas de analizará cada una de ellas y se meterán en la sección .text del código del ELF
4. Se generarán las tablas .symtab (tabla de símbolos), .strtab (tabla de nombres/cadenas) y .rela.text()
6. Se construirá la imagen ELF completa relocatable (foramto ELF64 ET_REL)
7. Se escribirá dicha imagen en el fichero .o 

He agregado soporte a más secciones, como resultado tengo las siguientes secciones (que también deben soportar relocaciones)

.text y .rela.text -----> instrucciones en ensamblador
.rodata y rela.rodata ---> datos de solo lectura, existen en memoria al ejecutar el programa pero no se modifican 
                            ej:.rodata

                                mensaje:
                                    .asciz "Hola mundo"
.data y .rela.data -----> datos inicializados que podrian ser modificados durante la ejecución
                          ej: .data

                              contador:
                                  .word 10
.bss ------> variables inicializadas a cero pero que no se almacenan en el fichero ensamblador. Por ejemplo al reservar un espacio de 1024 bytes, sería innecesario guardar todo 0s, solo se guarda el tamaño
Esta sección .bss no guarda relocaciones porque no hay bytes físicos en el fichero objeto
                          ej: .bss
                              buffer:
                                  .zero 256


SI una sección no llega a existir en el fichero en ensamblador, la cabecera de esa sección seguirá existiendo en el ELF relocatable pero con tamaño cero, así: 
.rodata:
    sh_size = 0

El formato de mi ELF es fijo, y el resultad ofinal con las nuevas secciones de fichero objeto relocatable es: 
0   NULL
1   .text
2   .rela.text
3   .rodata
4   .data
5   .bss
6   .rela.rodata
7   .rela.data
8   .symtab
9   .strtab
10  .shstrtab
*/

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_writer.h"

/*Como esta versión de xv6 no tiene "realloc" (llamada de C que cambia el tamaño de un bloque de memoria)
pues trabajo con tamaños fijos*/
#define XV6_TCC_AS_SOURCE_CAPACITY 16384 //límite del tamaño del fichero ensamblador a leer
#define XV6_TCC_AS_TEXT_CAPACITY 8192 //máximo número de bytes de las instrucciones codificadas. Cada instrucción ocupa 4 bytes, entonces 8192/4 = 2048 instrucciones
#define XV6_TCC_AS_RODATA_CAPACITY 4096
#define XV6_TCC_AS_DATA_CAPACITY 4096
#define XV6_TCC_AS_SYMTAB_CAPACITY 4096 //máximo tamaño en bytes reservado para la tabla de símbolos .symtab
#define XV6_TCC_AS_STRTAB_CAPACITY 4096
#define XV6_TCC_AS_RELA_CAPACITY 4096
#define XV6_TCC_AS_SHSTRTAB_CAPACITY 256 //la tabla ".shstrtab" guarda los nombres de todas las secciones del ELF (.text, .rela.text, .rodata, .data, .bss, .rela.rodata, .rela.data, .symtab, .strtab, .shstrtab)
#define XV6_TCC_AS_IMAGE_CAPACITY 32768 //Máximo tamaño del fichero objeto (cabecera ELF + secciones ELF + tabla de cabeceras de sección)

//arrays de almacenamiento 
//tipo "char" los búferes que usan texto. Tipo "uchar" los que puedden contener cualquier valor y no debe interpretarse como cadena
//pongo que estos búferes sea estáticos para que solo sean accesibles aquí y porque así la memoria no se reserva en la pila de la función (pila de usuario), 
//por lo que se colocan en la zona de datos del programa (zona BSS)
static char source_data[XV6_TCC_AS_SOURCE_CAPACITY]; //en este array se guardará el .s leído en crudo
static uchar text_data[XV6_TCC_AS_TEXT_CAPACITY]; //array de los bytes de las instrucciones codificadas
static uchar rodata_data[XV6_TCC_AS_RODATA_CAPACITY]; //array de los bytes de la seccion readonly data
static uchar data_data[XV6_TCC_AS_DATA_CAPACITY]; //array 
static uchar symtab_data[XV6_TCC_AS_SYMTAB_CAPACITY]; //array de la tabla de símbolos .symtab (cada entrada guarda la información estructurada del símbolo)
static char strtab_data[XV6_TCC_AS_STRTAB_CAPACITY]; //array de la tabla de la tabla de cadenas .strtab (cada entrada guarda solamente el nombre del símbolo)
static uchar rela_text_data[XV6_TCC_AS_RELA_CAPACITY]; //array de la tabla de relocaciones .re.text
static uchar rela_rodata_data[XV6_TCC_AS_RELA_CAPACITY];
static uchar rela_data_data[XV6_TCC_AS_RELA_CAPACITY];
static char shstrtab_data[XV6_TCC_AS_SHSTRTAB_CAPACITY]; //array de la tabla del nombre de las secciones elf (.text, .re.text, .symtab, .strtab, .shstrtab)
static uchar image_data[XV6_TCC_AS_IMAGE_CAPACITY]; //Aquí se almacenará todo los bytes del fichero ELF64 ET_REL
static struct Xv6TccObjectBuilder object;

/*EL verdadero ensamblador, basado en TInyCC*/

/*Leerá el fichero .s y guardará su contenido en el array "source_data"*/
static int
read_source_file(const char *path)
{
  int file;
  int total;

  //se abre el fichero como solo lectura
  file = open(path, O_RDONLY);
  if(file < 0)
    return -1;

  //inicializo el total de bytes leídos del fichero a 0
  total = 0;
  while(total < XV6_TCC_AS_SOURCE_CAPACITY - 1){
    int amount;

    /*se lee todo el fichero ensamblador .s menos un byte que es el nulo.
    Esto está dentro de un bucle porque no siempre see leen los XV6_TCC_AS_SOURCE_CAPACITY - 1 bytes, y al leerse todo los bytes, la condición del while se 
    deja de cumplir y se sale del bucle. Pero si digamos un caso hipotético en el que se leyera menos del límite, como 500 bytes, ahí si se darían vueltas
    Ej: se lee un fichero de 500 bytes, entonces primera vuelta "amount = 500" por lo que se repite el bucle. 
        segunda vuelta, se leen 0 bytes del fichero, entonces "amount = 0" ----> break y se sale del bucle*/
    amount = read(file, source_data + total,
                  XV6_TCC_AS_SOURCE_CAPACITY - 1 - total);
    //si hubo hubo un error en la lectura se sale
    if(amount < 0){
      close(file);
      return -1;
    }
    //si  no se ha leído nada es porque el fichero ensamblador está vacío y se sale del bucle
    if(amount == 0)
      break;
    //actualiza el total leído, se jecuta al menos 1 vez
    total += amount;
  }

  /*Comprobación que el fichero ensamblador leído no es más grande que el límit establecido. 
  El máximo de datos que se lee viene dado por "XV6_TCC_AS_SOURCE_CAPACITY - 1", por lo que 
  si se han leído exactamente esa cantidad de bytes, se comprueba si no todavía hay bytes por leer 
  y esos se almacenan en la variable auxiliar "extra"*/
  if(total == XV6_TCC_AS_SOURCE_CAPACITY - 1){
    char extra;

    //se intenta leer un byte del ensamblador, por lo que si se puede leer, es porque hemos excedido el límite y se sale con error
    if(read(file, &extra, 1) != 0){
      close(file);
      return -1;
    }
  }

  //si todo ha ido bien, se cierra el fichero
  if(close(file) < 0)
    return -1;

  /*al final del array con la lectura del ensamblador, se pone el nulo para convertir el contenido en una cadena de C
  EJ: si el ensamblador fuera:
  .text
  main:
  ret
  Entonces en "source_data" tendría = . t e x t \n m a i n : \n r e t \0*/
  source_data[total] = 0;
  return 0;
}

/*Esta función coge el código ensamblador en "source_data" y lo separará en líneas.
Recuerda, el array del código es una variable estática que puedo acceder a ella desde cualquier función de este fichero.
Me devolverá el fichero objeto contruido en memoria con todas las líneas ya parseadas*/
static int
process_source(struct Xv6TccObjectBuilder *object)
{
  //variable auxiliar para recorrer el array del ensambaldor
  char *cursor;
  //variable auxiliar para saber en qué línea del fichero me encuentro
  int line_number;

  //el cursor apunta al primer carácter del ensamblador
  cursor = source_data;
  //me encuentro en la primera línea
  line_number = 1;

  /*Reccorré todo el ensamblador hasta separar todas las líneas.
  Pondré un ejemplo de cómo funciona el bucle para la memoria*/
  while(1){
    struct Xv6TccParsedLine line; //Guardará el resultado de analizar la línea con el parser de líneas
    char *line_text; //apunta al inicio de la línea actual
    int has_next_line; //comprueba si hay un "\n" para ver si hay otra línea siguiente, ya que al final del fichero ensamblador habrá un caracter nulo \0, no un \n

    /*Pongo el "line_text" a apuntar al inicio de la línea, de forma que tendría el siguiente código (recuerda, los \n separan líneas):
    source_data:
    .text\n.globl main\nmain:\nret\0
    ^
    |
    cursor
    line_text  */
    line_text = cursor;
    /*Muevo el cursor hasta encontrar un salto de línea \n, line_text seguirá apuntando al inicio de la línea
    Ej:
    .text\n.globl main...
    ^    ^
    |    |
    |    cursor
    line_text*/
    while(*cursor && *cursor != '\n')
      cursor++;

    has_next_line = *cursor == '\n';
    /*Si hay una línea siguiente, el siguiente paso es sustituir el salto de línea \n por el caracter nulo \0, y luego desplazar el cursor.
    EJ:
    text\n.globl main... --------> text\0.globl main... --------->   text\0.globl main...
        ^                              ^                             ^     ^
        |                              |                             |     | 
        cursor                         cursor                        |     cursor
                                                                     line_text

                                                                     Como se observa al hacer esto, "cursor" se pone a apuntar al comienzo de la 
                                                                     siguiente línea, en cambio "line_text" apunta a una cadena independiente ".text\0"
                                                                     ya que termina en el caracter nulo.
                                                                     Con esto consigo modificar el fichero ensamblador en el array "source_Data" para separar 
                                                                     las líneas sin crear copias adicionales
    */
    if(has_next_line){
      *cursor = 0;
      cursor++;
    }

    /*la línea apuntada por "line_text" se manda a analizar/parsear y se guarda el resultado en la struct auxiliar "line"
    El procesado se hace en el fichero xv6_tcc_line pero un ejemplo sería:
    line_text = ".globl main"
    
    Una vez procesada, me quedaría así:
    line.kind = DIRECTIVE
    line.name = ".globl"
    line.operand_count = 1
    line.operands[0] = "main" */
    if(xv6_tcc_parse_line(line_text, &line) < 0){
      fprintf(2, "asxv6: linea %d: sintaxis invalida: %s\n",
              line_number, line_text);
      return -1;
    }

    /*metemos esa línea analizada en el fichero objeto de forma que:
    etiquetas --> símbolos
    .globl --> símbolos globales
    instrucciones --> bytes en .text
    saltos simbólicos --> relocaciones*/
    if(xv6_tcc_object_process_line(object, &line) < 0){
      fprintf(2, "asxv6: linea %d: no se puede ensamblar: %s\n",
              line_number, line_text);
      return -1;
    }

    /*si ya no hay líneas siguientes (has_next_line = 0), se sale del bucle, sino, se aumenta el contador de líneas.
    Si se hace otra iteración:
    line_number aumenta
    cursor ya  apunta al comienzo de la siguiente línea*/
    if(!has_next_line)
      break;
    line_number++;
  }

  return 0;
}

int
xv6_tcc_as_core(char *input, char *output)
{
  //struct Xv6TccObjectBuilder object; estado completo del fichero objeto en memoria
  struct Xv6TccElfBuffer text; //aquí almacenaré las instrucciones codificadas de la sección .text
  struct Xv6TccElfBuffer rodata;
  struct Xv6TccElfBuffer data;
  struct Xv6TccElfBuffer symtab; //tabla de símbolos
  struct Xv6TccElfStringTable strtab; //tabla de cadenas
  struct Xv6TccElfBuffer rela_text; //tabla de relocaciones de .text
  struct Xv6TccElfBuffer rela_rodata;
  struct Xv6TccElfBuffer rela_data;
  struct Xv6TccElfStringTable shstrtab; //tabla de nombres de las secciones
  struct Xv6TccElfBuffer image; //aquí irá la imagen completa del fichero objeto. Ósea el ELF relocatable, este array se escribirá en el fichero de salida .o

  //valido que los nombres de los ficheros no sean nulos
  if(!input || !output)
    return -1;

  if(strcmp(input, output) == 0){
    fprintf(2, "asxv6: entrada y salida no pueden ser el mismo fichero\n");
    return -1;
  }

  //leo el fichero ensamblador .s
  if(read_source_file(input) < 0){
    fprintf(2, "asxv6: no se puede leer %s\n", input);
    return -1;
  }

  //inicializo a la sección .text del objeto 
  text.data = text_data;
  text.size = 0;
  text.capacity = sizeof(text_data);

  rodata.data = rodata_data;
  rodata.size = 0;
  rodata.capacity = sizeof(rodata_data);

  data.data = data_data;
  data.size = 0;
  data.capacity = sizeof(data_data);

  //inicializo la tabla de símbolos
  symtab.data = symtab_data;
  symtab.size = 0;
  symtab.capacity = sizeof(symtab_data);

  //inicializo la tabla de cadena 
  strtab.data = strtab_data;
  strtab.size = 0;
  strtab.capacity = sizeof(strtab_data);

  //inicializo la tabla de relocaciones de .text
  rela_text.data = rela_text_data;
  rela_text.size = 0;
  rela_text.capacity = sizeof(rela_text_data);

  rela_rodata.data = rela_rodata_data;
  rela_rodata.size = 0;
  rela_rodata.capacity = sizeof(rela_rodata_data);

  rela_data.data = rela_data_data;
  rela_data.size = 0;
  rela_data.capacity = sizeof(rela_data_data);

  //inicializo la trabla de nombres de las secciones
  shstrtab.data = shstrtab_data;
  shstrtab.size = 0;
  shstrtab.capacity = sizeof(shstrtab_data);

  image.data = image_data;
  image.size = 0;
  image.capacity = sizeof(image_data);

  if(xv6_tcc_object_init_full(&object, &text, &rodata, &data,
                              &symtab, &strtab, &rela_text,
                              &rela_rodata, &rela_data) < 0){
    fprintf(2, "asxv6: no se pudo inicializar el objeto\n");
    return -1;
  }

  /*proceso el fichero fuente en ensamblador .s para dividirlo en líneas
  EJ: al final de esto podría tener en object: 
  text:
  16 bytes ---> instrucciones ya condificadas

  object.symbols: ---> la tabla de símbolos en memoria
  main
  done
  external_func

  object.relocations: ---> la tabla de relocaciones en memoria
  branch hacia done
  jal hacia external_func
  */
  if(process_source(&object) < 0)
    return -1;

  //convierte el estado en memoria del ELF en las tablas definitivas ELF
  if(xv6_tcc_object_finalize(&object) < 0){
    fprintf(2, "asxv6: no se pudieron finalizar simbolos y relocaciones\n");
    return -1;
  }

  /*Construye el ELF completo (con sus cabeceras y resto de info) y eso lo guarda en "image_data"
  Luego escribe "image_data" en el fichero de salida .o ELF relocatable. 
  Ósea primero crear la imagen ELF en memoria y luego la escribe en disco*/
  if(xv6_tcc_write_rel_object(&object, output,
                              &image, &shstrtab) < 0){
    fprintf(2, "asxv6: no se pudo escribir %s\n", output);
    return -1;
  }

  //imprime correcto funcionamiento
  fprintf(1, "asxv6: objeto ELF64 RISC-V ET_REL generado\n");
  fprintf(1, "  entrada: %s\n", input);
  fprintf(1, "  salida: %s\n", output);
  fprintf(1, "  .text: %d bytes\n", text.size); //imprime las instrucciones generadas
  fprintf(1, "  .rodata: %d bytes\n", rodata.size);
  fprintf(1, "  .data: %d bytes\n", data.size);
  fprintf(1, "  .bss: %d bytes\n", (int)object.bss_size);
  fprintf(1, "  .symtab: %d entradas\n",
          symtab.size / (int)sizeof(struct Xv6TccElfSym));
  fprintf(1, "  .rela.text: %d entradas\n",
          rela_text.size / (int)sizeof(struct Xv6TccElfRela));
  fprintf(1, "  .rela.rodata: %d entradas\n",
          rela_rodata.size / (int)sizeof(struct Xv6TccElfRela));
  fprintf(1, "  .rela.data: %d entradas\n",
          rela_data.size / (int)sizeof(struct Xv6TccElfRela));
  fprintf(1, "  ELF completo: %d bytes\n", image.size);
  return 0;
}
