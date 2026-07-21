#include "kernel/types.h"
#include "user/user.h"

static void
usage(void)
{
  fprintf(2, "uso: asxv6 -o salida.o entrada.s\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  char *input;
  char *output;

  if(argc != 4)
    usage();

  if(strcmp(argv[1], "-o") != 0)
    usage();

  output = argv[2];
  input = argv[3];

  printf("asxv6: programa integrado correctamente\n");
  printf("asxv6: entrada: %s\n", input);
  printf("asxv6: salida:  %s\n", output);

  exit(0);
}