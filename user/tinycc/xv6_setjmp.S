.text

.globl xv6_tcc_setjmp
.align 2
xv6_tcc_setjmp:
  sd ra,   0(a0)
  sd sp,   8(a0)

  sd s0,  16(a0)
  sd s1,  24(a0)
  sd s2,  32(a0)
  sd s3,  40(a0)
  sd s4,  48(a0)
  sd s5,  56(a0)
  sd s6,  64(a0)
  sd s7,  72(a0)
  sd s8,  80(a0)
  sd s9,  88(a0)
  sd s10, 96(a0)
  sd s11, 104(a0)

  li a0, 0
  ret


.globl xv6_tcc_longjmp
.align 2
xv6_tcc_longjmp:
  /*conserva temporalmente el valor de retorno antes de
  restaurar los registros guardados*/

  mv t0, a1

  ld ra,   0(a0)

  ld s0,  16(a0)
  ld s1,  24(a0)
  ld s2,  32(a0)
  ld s3,  40(a0)
  ld s4,  48(a0)
  ld s5,  56(a0)
  ld s6,  64(a0)
  ld s7,  72(a0)
  ld s8,  80(a0)
  ld s9,  88(a0)
  ld s10, 96(a0)
  ld s11, 104(a0)

  /*restaura la pila al final para evitar utilizarla después
  de recuperar el contexto anterior*/

  ld sp, 8(a0)

  mv a0, t0

  // Se convierte un valor cero en uno, como exige longjmp
  bnez a0, 1f
  li a0, 1

1:
  ret