#include "kernel/types.h"
#include "user/user.h"

#include <stdarg.h>

#include "xv6_alloc.h"
#include "xv6_tcc_runtime.h"

/*La biblioteca de usuario de este port ya implementa vprintf,
pero su declaración no aparece todavía en user.h*/

void vprintf(int fd, const char *format, va_list arguments);

static int tcc_runtime_error_count;

void
tcc_free(void *ptr)
{
  xv6_tcc_realloc(ptr, 0);
}

void *
tcc_malloc(unsigned long size)
{
  return xv6_tcc_realloc(0, size);
}

void *
tcc_realloc(void *ptr, unsigned long size)
{
  return xv6_tcc_realloc(ptr, size);
}

void *
tcc_mallocz(unsigned long size)
{
  void *ptr;

  ptr = tcc_malloc(size);
  if(ptr == 0)
    return 0;

  if(size != 0)
    memset(ptr, 0, (uint)size);

  return ptr;
}

char *
tcc_strdup(const char *str)
{
  char *copy;
  unsigned long length;

  if(str == 0)
    return 0;

  length = strlen(str);

  copy = tcc_malloc(length + 1);
  if(copy == 0)
    return 0;

  memmove(copy, str, (uint)length);
  copy[length] = 0;

  return copy;
}

static void
print_diagnostic(const char *prefix,
                 const char *format,
                 va_list arguments)
{
  fprintf(2, "asxv6: %s", prefix);
  vprintf(2, format, arguments);
  fprintf(2, "\n");
}

int
_tcc_error_noabort(const char *format, ...)
{
  va_list arguments;

  tcc_runtime_error_count++;

  va_start(arguments, format);
  print_diagnostic("error: ", format, arguments);
  va_end(arguments);

  return -1;
}

void
_tcc_error(const char *format, ...)
{
  va_list arguments;

  tcc_runtime_error_count++;

  va_start(arguments, format);
  print_diagnostic("error: ", format, arguments);
  va_end(arguments);

  /*finaliza provisionalmente el proceso, más adelante se
  sustituirá este comportamiento por setjmp y longjmp*/

  exit(1);
}

void
_tcc_warning(const char *format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  print_diagnostic("warning: ", format, arguments);
  va_end(arguments);
}

void
xv6_tcc_reset_error_count(void)
{
  tcc_runtime_error_count = 0;
}

int
xv6_tcc_get_error_count(void)
{
  return tcc_runtime_error_count;
}

int
xv6_tcc_check_runtime(void)
{
  unsigned char *buffer;
  unsigned char *resized_buffer;
  char *copy;
  int index;

  /*comprueba que tcc_mallocz reserva memoria inicializada
  completamente a cero*/

  buffer = tcc_mallocz(16);
  if(buffer == 0)
    return -1;

  for(index = 0; index < 16; index++){
    if(buffer[index] != 0){
      tcc_free(buffer);
      return -1;
    }
  }

  // Se escriben datos conocidos antes de ampliar el bloque

  for(index = 0; index < 16; index++)
    buffer[index] = (unsigned char)(index + 1);

  /*comprueba que tcc_realloc amplía el bloque y conserva
  correctamente todos los bytes anteriores*/

  resized_buffer = tcc_realloc(buffer, 32);
  if(resized_buffer == 0){
    tcc_free(buffer);
    return -1;
  }

  buffer = resized_buffer;

  for(index = 0; index < 16; index++){
    if(buffer[index] != (unsigned char)(index + 1)){
      tcc_free(buffer);
      return -1;
    }
  }

  // comprueba la duplicación de cadenas utilizada por TinyCC

  copy = tcc_strdup("tinycc-xv6");
  if(copy == 0){
    tcc_free(buffer);
    return -1;
  }

  if(strcmp(copy, "tinycc-xv6") != 0){
    tcc_free(copy);
    tcc_free(buffer);
    return -1;
  }

  tcc_free(copy);
  tcc_free(buffer);

  xv6_tcc_reset_error_count();

  if(xv6_tcc_get_error_count() != 0)
    return -1;

  return 0;
}