#ifndef XV6_TCC_CORE_H
#define XV6_TCC_CORE_H

#include "inttypes.h"

/*Se conservan los tamaños utilizados por el lexer original
de TinyCC*/

#define STRING_MAX_SIZE 1024
#define TOK_HASH_SIZE   16384
#define TOK_ALLOC_INCR  512
#define TOK_MAX_SIZE    4
#define IO_BUF_SIZE     8192

#define CH_EOB '\\'
#define CH_EOF (-1)

#define LDOUBLE_WORDS \
  ((sizeof(long double) + 3) / 4)

struct Sym;

/*Se conserva la estructura utilizada por TinyCC para asociar
un texto con su número de token*/

typedef struct TokenSym {
  struct TokenSym *hash_next;
  struct Sym *sym_define;
  struct Sym *sym_label;
  struct Sym *sym_struct;
  struct Sym *sym_identifier;

  int tok;
  int len;

  char str[1];
} TokenSym;

/*Se conserva el modelo de cadena dinámica utilizado por el
preprocesador y el lexer de TinyCC*/

typedef struct CString {
  int size;
  int size_allocated;
  char *data;
} CString;

/*Se conserva el valor asociado al token actual. Los números,
caracteres y cadenas se almacenarán en esta unión*/

typedef union CValue {
  long double ld;
  double d;
  float f;
  uint64_t i;

  struct {
    char *data;
    int size;
  } str;

  int tab[LDOUBLE_WORDS];
} CValue;

/*Se conserva la secuencia de tokens que TinyCC utiliza para
macros, repeticiones y devolución de tokens*/

typedef struct TokenString {
  int *str;
  int len;
  int need_spc;
  int allocated_len;
  int last_line_num;
  int save_line_num;

  struct TokenString *prev;
  const int *prev_ptr;

  char alloc;
} TokenString;

/*Se conserva la estructura de entrada original de TinyCC.
El campo buffer se amplía dinámicamente durante la reserva*/

typedef struct BufferedFile {
  uint8_t *buf_ptr;
  uint8_t *buf_end;

  int fd;

  struct BufferedFile *prev;

  int line_num;
  int line_ref;
  int ifndef_macro;
  int ifndef_macro_saved;

  int *ifdef_stack_ptr;

  int include_next_index;
  int prev_tok_flags;

  char filename[1024];
  char *true_filename;

  unsigned char unget[4];
  unsigned char buffer[1];
} BufferedFile;

#endif