#ifndef XV6_TCC_TOKENS_H
#define XV6_TCC_TOKENS_H

/*Se utiliza la misma configuración de tokens que TinyCC para
un objetivo RISC-V de 64 bits*/

#ifndef TCC_TARGET_RISCV64
#define TCC_TARGET_RISCV64 1
#endif

#ifndef PTR_SIZE
#define PTR_SIZE 8
#endif

/*TinyCC reserva los valores inferiores a 256 para caracteres,
operadores y tokens especiales*/

#define TOK_EOF      (-1)
#define TOK_LINEFEED 10
#define TOK_IDENT    256

/*Se genera el mismo enum que aparece en tcc.h. tcctok.h incluye
las directivas generales y, al final, riscv64-tok.h*/

enum Xv6TccToken {
  TOK_LAST = TOK_IDENT - 1,

#define DEF(token, text) token,

#include "tcctok.h"

#undef DEF
#undef DEF_ASM
#undef DEF_ASMDIR
#undef DEF_ATOMIC

  XV6_TCC_TOKEN_END
};

#define XV6_TCC_TOKEN_COUNT \
  (XV6_TCC_TOKEN_END - TOK_IDENT)

// Se busca un token por su representación textual
int xv6_tcc_find_token(const char *text);

// Se devuelve el texto asociado a un token conocido
const char *xv6_tcc_token_name(int token);

// Se devuelve el número de tokens importados
int xv6_tcc_token_count(void);

#endif

/*Genero los identificadores de los tokens utilizando directamente
la tabla original riscv64-tok.h de TinyCC

enum Xv6TccToken {
  XV6_TCC_TOKEN_INVALID = -1,

#define DEF(token, text) token,
#define DEF_ASM(name) DEF(TOK_ASM_##name, #name)

#include "riscv64-tok.h"

#undef DEF_ASM
#undef DEF

  XV6_TCC_TOKEN_COUNT
};

// SE busca un token RISC-V por su representación textual
int xv6_tcc_find_token(const char *text);

// Devuelvo el texto asociado a un identificador de token
const char *xv6_tcc_token_name(int token);

// Devuelvo el número de tokens importados desde TinyCC
int xv6_tcc_token_count(void);

#endif*/