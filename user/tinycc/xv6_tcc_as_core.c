#include "kernel/types.h"
#include "user/user.h"

/*EL verdadero ensamblador, basado en TInyCC*/
int
xv6_tcc_as_core(char *input, char *output)
{
  fprintf(1, "[etapa 1] asxv6 recibio la peticion\n");
  fprintf(1, "  entrada: %s\n", input);
  fprintf(1, "  salida:  %s\n", output);
  fprintf(1, "  capa de buffers ELF: disponible\n");
  fprintf(1, "  codificadores RV64I R/I/S/B/U/J: disponibles\n");
  fprintf(2, "El parser de operandos se hará en el siguiente commit\n");
  return -1;
}
