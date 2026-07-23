#ifndef XV6_TCC_EMIT_H
#define XV6_TCC_EMIT_H

#include "xv6_section.h"

/*Se selecciona la sección en la que se emitirán los bytes
generados por el backend*/

int xv6_tcc_emit_begin(Section *section);

// Se habilita o deshabilita temporalmente la generación de código
void xv6_tcc_emit_set_disabled(int disabled);

// Se escribe un byte en la sección activa
void xv6_tcc_g(int value);

// Se escribe un valor de 16 bits en formato little-endian
void xv6_tcc_gen_le16(int value);

// Se escribe un valor de 32 bits en formato little-endian
void xv6_tcc_gen_le32(int value);

// Se devuelve el estado de la última operación de emisión
int xv6_tcc_emit_status(void);

// Se comprueba la emisión de instrucciones RISC-V conocidas
int xv6_tcc_check_emitter(void);

#endif