/*
xv6_tcc_ld_core.c

Coordinador del linker.
Carga y valida objetos ELF64 RISC-V ET_REL. Todavia no combina secciones,
resuelve simbolos ni genera el ejecutable final.
*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_elf_reader.h"

#define XV6_TCC_LD_MAX_INPUTS 8 //máximo número de ficheros objeto de entrada que permito EJ: ldxv6 uno.o dos.o tres.o -o programa
#define XV6_TCC_LD_OBJECT_CAPACITY 32768 //máximo tamaño en bytes de cada fichero objeto. Coincide con el tamaño que puse en la creación del .o en xv6_tcc_as_core.c

static uchar object_data[XV6_TCC_LD_MAX_INPUTS][XV6_TCC_LD_OBJECT_CAPACITY];/*Es la matriz para leer los posibles 8 ficheros objeto como entrada. Cada entrada serán los bytes 
en crudo de ese fichero Objeto EJ 
object_data[0] -> 32768 bytes para el primer objeto
object_data[1] -> 32768 bytes para el segundo objeto*/
static struct Xv6TccElfBuffer object_storage[XV6_TCC_LD_MAX_INPUTS]; /*Va a haber un puntero hacía el buffer del fichero objeto de cada entrada, a sus campos como estructura, con los siguientes campos:
struct Xv6TccElfBuffer {
  uchar *data;  -------->  aquí almacenaré los bytes del fichero objeto en cuestión, ósea el puntero a "object_data"
  uint size;
  uint capacity;
};*/
static struct Xv6TccRelObjectView object_views[XV6_TCC_LD_MAX_INPUTS]; /*Es una vista a los demás elementos del fichero objeto de cada entrada (es una vista del ELF raw):  secciones de .text, símbolos, cadenas, relocaciones y cabeceras de sección*/

/*Imprime un resumen del fichero objeto. 
Recibe: 
- index: el índice el fichero objeto entre los ficheros objetos entrantes
- path: el nombre del fichero
- view: vista del fichero objeto, contiene secciones de .text, símbolos, cadenas, relocaciones y cabeceras de sección
*/
static void
print_object_summary(int index, const char *path,
                     const struct Xv6TccRelObjectView *view)
{
  //Nombre del fichero objeto y su posición en el array de objetos de entrada: Ej: entrada[0]: stage9.o
  fprintf(1, "  entrada[%d]: %s\n", index, path);
  //tamaño del fichero ELF relocatable
  fprintf(1, "    ELF: ET_REL RISC-V, %d bytes\n", view->size);
  //Número de secciones del fichero ELF. El # de secciones está en la cabecera ELF, en el campo e_shnum
  fprintf(1, "    secciones: %d\n", view->header->e_shnum);
  //revisamos la cabecera de la sección text (el campo sh_size) para saber el número de bytes
  fprintf(1, "    .text: %d bytes\n", (int)view->text_section->sh_size);
  //imprime el número de símbolos de la tabla de símbolos y el índice del primer símbolo no local (global) de la tabla de símbolos
  //Ej: .symtab: 4 entradas, primer global=2
  fprintf(1, "    .symtab: %d entradas, primer global=%d\n",
          view->symbol_count,
          (int)view->symtab_section->sh_info);
  //imprime el número de relocaciones o referencias simbólicas en .rela.text
  fprintf(1, "    .rela.text: %d entradas\n",
          view->relocation_count);
}

/*Función principal del linker: se encarga de enlazar todos los ficheros objetos entrantes en un fichero ELF ejecutable. De momento no genero el ELF ejecutable, solo lo analizo.
Recibe:
- input_count --> num de ficheros objeto entrantes
- inputs --> array con los nombres de los ficheros objetos
- output --> será el fichero ELF ejecutable a devolver*/
int
xv6_tcc_ld_core(int input_count, char **inputs, char *output)
{
  int i;

  //valido que el número de ficheros objetos entrantes sea mayor a 0 y menos a XV6_TCC_LD_MAX_INPUTS
  if(input_count <= 0 || input_count > XV6_TCC_LD_MAX_INPUTS ||
     !inputs || !output)
    return -1;

  fprintf(1, "ldxv6: lectura de objetos ELF64 ET_REL\n");

  /*Bucle para recorrer todos los ficheros objetos entrantes*/
  for(i = 0; i < input_count; i++){

    /*Se prepara el buffer objeto auxiliar para inspeccionar el fichero objeto actual*/

    //el campo data del buffer objeto apuntará a los bytes en crudo del objeto
    object_storage[i].data = object_data[i];
    //se pone que está vacío porque aún no ha leído nada
    object_storage[i].size = 0;
    //la capacidad de almacenamiento del fichero objeto en crudo será igual al máximo tamaño posible del objeto, ósea XV6_TCC_LD_OBJECT_CAPACITY
    object_storage[i].capacity = sizeof(object_data[i]);


    /*Se va a cargar el fichero objeto en memoria (en el object_storage) y se analizarán sus bytes para ver que estén correctos, estos son los pasos:
    1. Cargará el fichero objeto con nombre input[i] en la estructura auxiliar del objeto "object_storage[i]"
    2. Analizará y validará los bytes de ese fichero ELF relocatable y dejará el resultado en "object_views[i]"*/
    if(xv6_tcc_load_rel_object(inputs[i], &object_storage[i], &object_views[i]) < 0){

      /*El fichero objeto será inválido sí:
      el fichero no existe
      no se puede abrir
      supera el máximo tamaño fijo
      está vacío 
      no tiene magic ELF
      no es ELF64
      no es little-endian
      no es RISC-V
      no es ET_REL
      alguna sección queda fuera del fichero
      falta .symtab o .strtab
      una cadena no termina en cero
      una relocación usa un símbolo inexistente
      una relocación apunta fuera de .text*/
      fprintf(2, "ldxv6: objeto invalido o no legible: %s\n",
              inputs[i]);
      return -1;
    }

    /*Si se ha validad el fichero objeto, se imprime su información*/
    print_object_summary(i, inputs[i], &object_views[i]);
  }

  //Todvaía no creo el fichero ELF ejecutable!!
  fprintf(1, "  salida solicitada: %s\n", output);
  fprintf(1, "  validacion completada; aun no se genera ET_EXEC\n");
  return 0;
}
