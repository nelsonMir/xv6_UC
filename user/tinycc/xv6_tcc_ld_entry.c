#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_entry.h"

/*Este fichero va a ofrecer los wrapper para conectar ldxv6 <---> tcc_ld_core*/

int xv6_tcc_ld_core(int input_count, char **inputs, char *output);

int
xv6_tcc_link_files(int input_count, char **inputs, char *output)
{
  return xv6_tcc_ld_core(input_count, inputs, output);
}
