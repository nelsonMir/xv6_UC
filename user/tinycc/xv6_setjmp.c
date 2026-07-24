#include "kernel/types.h"

#include "xv6_setjmp.h"

static volatile int jump_was_requested;

int
xv6_tcc_check_setjmp(void)
{
  Xv6TccJmpBuf environment;
  int result;

  jump_was_requested = 0;

  result = xv6_tcc_setjmp(&environment);

  if(result == 0){
    jump_was_requested = 1;

    xv6_tcc_longjmp(&environment, 37);
  }

  /*comprueba que el control ha regresado al punto guardado
  y que se ha conservado el valor solicitado*/

  if(result != 37)
    return -1;

  if(jump_was_requested != 1)
    return -1;

  return 0;
}