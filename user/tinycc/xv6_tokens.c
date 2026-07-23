#include "kernel/types.h"
#include "user/user.h"

#include "xv6_tokens.h"

/*Genero una tabla de cadenas incluyendo por segunda vez la misma
tabla original que he utilizado para generar el enum*/

static const char *const token_names[] = {

#define DEF(token, text) text,

#include "tcctok.h"

#undef DEF
#undef DEF_ASM
#undef DEF_ASMDIR
#undef DEF_ATOMIC
};

int
xv6_tcc_find_token(const char *text)
{
  int index;

  if(text == 0)
    return TOK_EOF;

  /*Se recorren los tokens generales, las directivas y los
  tokens específicos de RISC-V*/

  for(index = 0;
      index < XV6_TCC_TOKEN_COUNT;
      index++){
    if(strcmp(text, token_names[index]) == 0)
      return TOK_IDENT + index;
  }

  return TOK_EOF;
}

const char *
xv6_tcc_token_name(int token)
{
  int index;

  if(token < TOK_IDENT ||
     token >= XV6_TCC_TOKEN_END)
    return 0;

  index = token - TOK_IDENT;

  return token_names[index];
}

int
xv6_tcc_token_count(void)
{
  return XV6_TCC_TOKEN_COUNT;
}