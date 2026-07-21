#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "xv6_tcc.h"

/*
 * Punto de entrada del ensamblador TinyCC para xv6.
 *
 * En esta etapa solo comprobamos que:
 *
 *   1. asxv6 puede llamar a código situado en user/tinycc/.
 *   2. El ensamblador puede abrir el archivo de entrada.
 *   3. El ejecutable puede componerse a partir de varios .o.
 *
 * La lógica real de TinyCC se añadirá en los próximos pasos.
 */

  /*Esta función ensambla input_path (el fichero en esa dirección) y escribirá 
 un ELF relocatable en output_path. 
 Esta función será el punto de entrada del emsamblador TInyCC adaptado para xv6, ósea
 la usaré como interfaz entre el asxv6.c y el TinyCC adaptado de forma que el compilaro de 
 xv6 no se tenga que enterar de las estructuras internas de TinyCC que se utilizarán del compilador original
 
 Actualmente solo se comprobará que:
 1. asxv6 puede llamar al código situado en el directorio /user/tinycc/ ya que no pondré los ficheros del ensamblador tinyCC
 directamente en el directorio user 
 2. el ensambaldor puede leer el fichero de entrada 
 3. el ejecutable puede componerse de varios.o */
int
xv6_tcc_assemble(const char *input_path,
                 const char *output_path)
{
  int fd;

  /*
   output_path todavía no se usa.
   Esta conversión evita un aviso de variable no utilizada, que con -Werror impediría compilar
   */
  (void)output_path;

  fd = open(input_path, O_RDONLY);
  if(fd < 0)
    return XV6_TCC_ERR_INPUT;

  close(fd);

  return XV6_TCC_ERR_NOT_READY;
}