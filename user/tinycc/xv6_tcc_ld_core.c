#include "kernel/types.h"
#include "user/user.h"

/*EL verdadero LInker, basado en TINyCC*/
int
xv6_tcc_ld_core(int input_count, char **inputs, char *output)
{
  int i;

  fprintf(1, "[etapa 1] ldxv6 recibio la peticion\n");
  fprintf(1, "  objetos: %d\n", input_count);
  for(i = 0; i < input_count; i++)
    fprintf(1, "  entrada[%d]: %s\n", i, inputs[i]);
  fprintf(1, "  salida: %s\n", output);
  fprintf(1, "  capa de buffers ELF: disponible\n");
  fprintf(2, "ldxv6: el enlazado se implementara en etapas posteriores\n");
  return -1;
}
