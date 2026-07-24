#ifndef XV6_TCC_RUNTIME_H
#define XV6_TCC_RUNTIME_H

/*Se declaran las funciones de memoria con las mismas firmas
que utiliza internamente TinyCC*/

void tcc_free(void *ptr);
void *tcc_malloc(unsigned long size);
void *tcc_mallocz(unsigned long size);
void *tcc_realloc(void *ptr, unsigned long size);
char *tcc_strdup(const char *str);

/*las funciones básicas de diagnóstico utilizadas
por el lexer y el parser originales de TinyCC*/

int _tcc_error_noabort(const char *format, ...)
  __attribute__((format(printf, 1, 2)));

void _tcc_error(const char *format, ...)
  __attribute__((noreturn))
  __attribute__((format(printf, 1, 2)));

void _tcc_warning(const char *format, ...)
  __attribute__((format(printf, 1, 2)));

// reinicia el contador provisional de errores
void xv6_tcc_reset_error_count(void);

// devuelve el número provisional de errores
int xv6_tcc_get_error_count(void);

// comprueba el funcionamiento del runtime de memoria
int xv6_tcc_check_runtime(void);

#endif