#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_entry.h"

/*Programa de usuario para interactuar con el ensamblador que esta en tinyCC*/

/*Imprime el uso correcto del comando, hay 2 formas de usarlo*/
static void
usage(void)
{
  fprintf(2, "uso: asxv6 entrada.s -o salida.o\n");
  fprintf(2, "     asxv6 -o salida.o entrada.s\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  char *input;
  char *output;

  //comprueba el número de argumentos, si es diferente de 4, imprime el uso correcto
  if(argc != 4)
    usage();

  /*Identifica las 2 formas de utilizar el comando y que esté bien puesto.
  UNa vez comprobado, asigna en variables el fichero de entrada y el de salida */
  if(strcmp(argv[1], "-o") == 0){
    output = argv[2];
    input = argv[3];
  } else if(strcmp(argv[2], "-o") == 0){
    input = argv[1];
    output = argv[3];
  } else {
    usage();
    return 1;
  }

  /*Intenta generar el ensamblador llamando a la siguiente función del fichero núcleo/core del ensamblador
  (el ensamblador en sí), si falla se sale*/
  if(xv6_tcc_assemble_file(input, output) < 0)
    exit(1);
  exit(0);
}
