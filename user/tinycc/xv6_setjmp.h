#ifndef XV6_TCC_SETJMP_H
#define XV6_TCC_SETJMP_H

#include "kernel/types.h"

/*Se guardan los registros preservados por las funciones según
la convención de llamadas de RISC-V de 64 bits*/

typedef struct Xv6TccJmpBuf {
  uint64 ra;
  uint64 sp;

  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
} Xv6TccJmpBuf;

/* indica al compilador que esta función puede devolver
más de una vez*/

int xv6_tcc_setjmp(Xv6TccJmpBuf *environment)
  __attribute__((returns_twice));

/*restaura el contexto guardado y se regresa al punto en el
que se llamó anteriormente a xv6_tcc_setjmp*/

void xv6_tcc_longjmp(Xv6TccJmpBuf *environment,
                     int value)
  __attribute__((noreturn));

//comprueba que el salto no local funciona correctamente
int xv6_tcc_check_setjmp(void);

#endif