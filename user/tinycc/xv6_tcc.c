#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "xv6_alloc.h"
#include "xv6_backend.h"
#include "xv6_elf.h"
#include "xv6_section.h"
#include "xv6_tokens.h"
#include "xv6_tcc.h"

#define XV6_TCC_READ_CHUNK 4096
#define XV6_TCC_MAX_SOURCE 0x7ffffffeUL

static int
read_source_file(const char *path,
                 char **buffer_result,
                 uint64 *size_result)
{
  struct stat st;
  char *buffer;
  uint64 total;
  int remaining;
  int chunk;
  int count;
  int fd;

  *buffer_result = 0;
  *size_result = 0;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return XV6_TCC_ERR_INPUT;

  if(fstat(fd, &st) < 0){
    close(fd);
    return XV6_TCC_ERR_STAT;
  }

  if(st.size > XV6_TCC_MAX_SOURCE){
    close(fd);
    return XV6_TCC_ERR_TOO_LARGE;
  }

  /*Añado un byte al final para terminar el contenido con cero
  y poder tratarlo posteriormente como una cadena*/

  buffer = xv6_tcc_realloc(0, (unsigned long)st.size + 1);
  if(buffer == 0){
    close(fd);
    return XV6_TCC_ERR_MEMORY;
  }

  total = 0;

  while(total < st.size){
    remaining = (int)(st.size - total);

    if(remaining > XV6_TCC_READ_CHUNK)
      chunk = XV6_TCC_READ_CHUNK;
    else
      chunk = remaining;

    count = read(fd, buffer + total, chunk);

    if(count <= 0){
      xv6_tcc_realloc(buffer, 0);
      close(fd);
      return XV6_TCC_ERR_READ;
    }

    total += count;
  }

  buffer[total] = 0;

  close(fd);

  *buffer_result = buffer;
  *size_result = total;

  return XV6_TCC_OK;
}

/*Esta función ensambla input_path (el fichero en esa dirección) y escribirá 
 un ELF relocatable en output_path. 
 Esta función será el punto de entrada del emsamblador TInyCC adaptado para xv6, ósea
 la usaré como interfaz entre el asxv6.c y el TinyCC adaptado de forma que el compilaro de 
 xv6 no se tenga que enterar de las estructuras internas de TinyCC que se utilizarán del compilador original
 */
int
xv6_tcc_assemble(const char *input_path,
                 const char *output_path)
{
  char *source;
  uint64 source_size;
  int result;

  // La salida comenzará a utilizarse al incorporar el generador ELF
  (void)output_path;

  result = read_source_file(input_path,
                            &source,
                            &source_size);

  if(result != XV6_TCC_OK)
    return result;

  printf("asxv6: cargados %d bytes desde %s\n", (int)source_size, input_path);

  /*Compruebo que la tabla original de TinyCC ha generado
  correctamente varios tokens representativos*/

  if(xv6_tcc_find_token("addi") != TOK_ASM_addi ||
    xv6_tcc_find_token("ld") != TOK_ASM_ld ||
    xv6_tcc_find_token("ret") != TOK_ASM_ret ||
    xv6_tcc_find_token("c.addi") != TOK_ASM_c_addi){
    xv6_tcc_realloc(source, 0);
    return XV6_TCC_ERR_TOKEN_TABLE;
  }

  printf("asxv6: tabla RISC-V de TinyCC cargada\n");
  printf("asxv6: %d tokens disponibles\n", xv6_tcc_token_count());

  /*Se comprueba que las estructuras ELF64 mantienen los tamaños
  y valores esperados para un objeto relocatable RISC-V*/

  if(xv6_tcc_check_elf_abi() < 0){
    xv6_tcc_realloc(source, 0);
    return XV6_TCC_ERR_ELF_ABI;
  }

  printf("asxv6: definiciones ELF64 de TinyCC validadas\n");

  /*Se comprueba que el backend RISC-V original se ha incluido
  correctamente con las definiciones*/

  if(xv6_tcc_check_riscv_backend() < 0){
    xv6_tcc_realloc(source, 0);
    return XV6_TCC_ERR_BACKEND;
  }

  printf("asxv6: backend RISC-V de TinyCC validado\n");
  printf("asxv6: %d registros disponibles\n",
         xv6_tcc_backend_register_count());

    /*Se comprueba que una sección puede crecer, conservar sus
  datos y aplicar alineamiento interno*/

  if(xv6_tcc_check_section_model() < 0){
    xv6_tcc_realloc(source, 0);
    return XV6_TCC_ERR_SECTION;
  }

  printf("asxv6: modelo de secciones de TinyCC validado\n");

  
  /*Libero ahora el texto porque todavía no he conectado
  el lexer y el parser de TinyCC*/

  xv6_tcc_realloc(source, 0);

  return XV6_TCC_ERR_NOT_READY;
}