#include "kernel/types.h"
#include "user/user.h"

#include "tinycc/xv6_tcc.h"

static void
usage(void)
{
  fprintf(2, "uso: asxv6 -o salida.o entrada.s\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  const char *input;
  const char *output;
  int result;

  if(argc != 4)
    usage();

  if(strcmp(argv[1], "-o") != 0)
    usage();

  output = argv[2];
  input = argv[3];

  result = xv6_tcc_assemble(input, output);

  if(result == XV6_TCC_OK){
    printf("asxv6: objeto generado: %s\n", output);
    exit(0);
  }

  if(result == XV6_TCC_ERR_INPUT){
    fprintf(2, "asxv6: no se puede abrir %s\n", input);
    exit(1);
  }

  if(result == XV6_TCC_ERR_STAT){
    fprintf(2,
            "asxv6: no se puede obtener el tamaño de %s\n",
            input);
    exit(1);
  }

  if(result == XV6_TCC_ERR_READ){
    fprintf(2,
            "asxv6: error leyendo %s\n",
            input);
    exit(1);
  }

  if(result == XV6_TCC_ERR_MEMORY){
    fprintf(2,
            "asxv6: no hay memoria suficiente\n");
    exit(1);
  }

  if(result == XV6_TCC_ERR_TOO_LARGE){
    fprintf(2,
            "asxv6: el archivo de entrada es demasiado grande\n");
    exit(1);
  }

  if(result == XV6_TCC_ERR_NOT_READY){
    fprintf(2,
            "asxv6: el archivo se ha cargado correctamente\n");
    fprintf(2,
            "asxv6: el parser de TinyCC aun no esta integrado\n");
    exit(1);
  }

  fprintf(2, "asxv6: error interno desconocido\n");
  exit(1);
}