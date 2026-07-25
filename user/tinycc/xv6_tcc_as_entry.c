#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_entry.h"

/*Este fichero va a ofrecer los wrapper para conectar asxv6 <---> tcc_as_core*/

int xv6_tcc_as_core(char *input, char *output);

int
xv6_tcc_assemble_file(char *input, char *output)
{
  return xv6_tcc_as_core(input, output);
}
