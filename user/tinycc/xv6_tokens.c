#include "kernel/types.h"
#include "user/user.h"

#include "xv6_tokens.h"

/*Genero una tabla de cadenas incluyendo por segunda vez la misma
tabla original que he utilizado para generar el enum*/

static const char *const token_names[] = {

#define DEF(token, text) text,
#define DEF_ASM(name) DEF(TOK_ASM_##name, #name)

#include "riscv64-tok.h"

#undef DEF_ASM
#undef DEF
};

int
xv6_tcc_find_token(const char *text)
{
  int token;

  if(text == 0)
    return XV6_TCC_TOKEN_INVALID;

  //Se recorre la tabla hasta encontrar el nombre solicitado
  for(token = 0; token < XV6_TCC_TOKEN_COUNT; token++){
    if(strcmp(text, token_names[token]) == 0)
      return token;
  }

  return XV6_TCC_TOKEN_INVALID;
}

const char *
xv6_tcc_token_name(int token)
{
  if(token < 0 || token >= XV6_TCC_TOKEN_COUNT)
    return 0;

  return token_names[token];
}

int
xv6_tcc_token_count(void)
{
  return XV6_TCC_TOKEN_COUNT;
}