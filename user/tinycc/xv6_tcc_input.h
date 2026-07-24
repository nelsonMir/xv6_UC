#ifndef XV6_TCC_INPUT_H
#define XV6_TCC_INPUT_H

#include "xv6_tcc_core.h"

/*Se crea una entrada compatible con TinyCC a partir del
contenido que ya se ha leído desde xv6*/

BufferedFile *xv6_tcc_input_new(
  const char *filename,
  const char *source,
  unsigned long source_size
);

// Se libera una entrada creada para el lexer
void xv6_tcc_input_delete(BufferedFile *input);

// Se comprueba la construcción del BufferedFile
int xv6_tcc_check_input(
  const char *filename,
  const char *source,
  unsigned long source_size
);

#endif