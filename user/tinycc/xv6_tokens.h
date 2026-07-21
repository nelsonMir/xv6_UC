#ifndef XV6_TCC_TOKENS_H
#define XV6_TCC_TOKENS_H

/*Genero los identificadores de los tokens utilizando directamente
la tabla original riscv64-tok.h de TinyCC*/

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

#endif