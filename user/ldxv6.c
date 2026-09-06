#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_entry.h"

/*Programa de usuario para interactuar con el enlazador que esta en tinyCC*/

/*El enlazador acepta uno o más ficheros objeto para ensamblar*/
static void
usage(void)
{
  fprintf(2, "uso: ldxv6 objeto1.o [objeto2.o ...] -o programa\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  /*SI el número de argumentos es menor a 4, imprime el uso correcto*/
  if(argc < 4 || strcmp(argv[argc - 2], "-o") != 0)
    usage();

  /*Calcula varias cosas que necesita la función siguiente que parte del core/núcelo del enlazador 
  (tcc_ld_core):
  
    - input_count = argc - 3: Es el número de ficheros objetos como entrada para el ejecutable saliente (se resta
      el comando, flag -o, el nombre del ejecutable saliente por eso restamos 3)
      
    - inputs = argv + 1: es un puntero al primer fichero objeto (por eso le sumo 1, para no comenzar con el comando)
    
    - output = argv[argc -1]: Es el nombre del fichero ejecutable como salida*/
  if(xv6_tcc_link_files(argc - 3, argv + 1, argv[argc - 1]) < 0)
    exit(1);
  exit(0);
}
