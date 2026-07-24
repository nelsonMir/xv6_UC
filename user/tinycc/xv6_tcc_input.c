#include "kernel/types.h"
#include "user/user.h"

#include "xv6_tcc_input.h"
#include "xv6_tcc_runtime.h"

#define XV6_TCC_FILENAME_LIMIT 1023

static void
copy_filename(char *destination,
              const char *source)
{
  int index;

  index = 0;

  if(source != 0){
    while(index < XV6_TCC_FILENAME_LIMIT &&
          source[index] != 0){
      destination[index] = source[index];
      index++;
    }
  }

  destination[index] = 0;
}

BufferedFile *
xv6_tcc_input_new(const char *filename,
                  const char *source,
                  unsigned long source_size)
{
  BufferedFile *input;
  unsigned long allocation_size;

  if(source == 0)
    return 0;

  /*La estructura ya contiene un byte de buffer. Se reserva
  espacio adicional para el archivo y el marcador CH_EOB*/

  allocation_size =
    sizeof(BufferedFile) + source_size;

  if(allocation_size < source_size)
    return 0;

  input = tcc_mallocz(allocation_size);
  if(input == 0)
    return 0;

  copy_filename(input->filename, filename);

  input->true_filename = input->filename;
  input->fd = -1;
  input->line_num = 1;
  input->line_ref = 1;

  if(source_size != 0){
    memmove(input->buffer,
            source,
            (uint)source_size);
  }

  /*Se coloca el mismo marcador que espera handle_eob() en
  el límite final del buffer*/

  input->buffer[source_size] = CH_EOB;

  input->buf_ptr = input->buffer;
  input->buf_end = input->buffer + source_size;

  return input;
}

void
xv6_tcc_input_delete(BufferedFile *input)
{
  tcc_free(input);
}

int
xv6_tcc_check_input(const char *filename,
                    const char *source,
                    unsigned long source_size)
{
  BufferedFile *input;

  input = xv6_tcc_input_new(
    filename,
    source,
    source_size
  );

  if(input == 0)
    return -1;

  if(input->fd != -1 ||
     input->line_num != 1 ||
     input->line_ref != 1){
    xv6_tcc_input_delete(input);
    return -1;
  }

  if(input->buf_ptr != input->buffer ||
     input->buf_end != input->buffer + source_size){
    xv6_tcc_input_delete(input);
    return -1;
  }

  if(input->buffer[source_size] != CH_EOB){
    xv6_tcc_input_delete(input);
    return -1;
  }

  if(source_size != 0 &&
     memcmp(input->buffer,
            source,
            (uint)source_size) != 0){
    xv6_tcc_input_delete(input);
    return -1;
  }

  if(input->true_filename != input->filename){
    xv6_tcc_input_delete(input);
    return -1;
  }

  if(filename != 0 &&
     strcmp(input->filename, filename) != 0){
    xv6_tcc_input_delete(input);
    return -1;
  }

  xv6_tcc_input_delete(input);

  return 0;
}