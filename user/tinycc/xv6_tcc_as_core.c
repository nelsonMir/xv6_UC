#include "kernel/types.h"
#include "user/user.h"

/*EL verdadero ensamblador, basado en TInyCC*/
int
xv6_tcc_as_core(char *input, char *output)
{
  fprintf(1, "[etapa 1] asxv6 recibio la peticion\n");
  fprintf(1, "  entrada: %s\n", input);
  fprintf(1, "  salida:  %s\n", output);
  fprintf(2, "asxv6: el ensamblado se implementara en etapas posteriores\n");
  return -1;
}
