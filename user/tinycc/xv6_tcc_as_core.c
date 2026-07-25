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
  fprintf(2, "asxv6: la codificacion RV64I comenzara en etapas posteriores\n");
  return -1;
}
