#include "kernel/types.h"

#include "xv6_backend.h"

/*Se incluye el archivo original en su modo de definiciones

De momento todavía no se compilan el parser ni los emisores*/

#define ST_FUNC extern
#define TARGET_DEFS_ONLY

#include "riscv64-asm.c"

#undef TARGET_DEFS_ONLY
#undef ST_FUNC

int
xv6_tcc_check_riscv_backend(void)
{
  // Se comprueba el número total de registros del backend
  if(NB_ASM_REGS != 64)
    return -1;

#ifndef CONFIG_TCC_ASM
  return -1;
#endif

  return 0;
}

int
xv6_tcc_backend_register_count(void)
{
  return NB_ASM_REGS;
}