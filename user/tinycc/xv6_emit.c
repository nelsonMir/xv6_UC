#include "kernel/types.h"
#include "user/user.h"

#include "xv6_emit.h"

/*Estas variables representan el estado mínimo utilizado por las
funciones g(), gen_le16() y gen_le32() del backend original*/

static Section *current_section;
static unsigned long current_offset;
static int code_generation_disabled;
static int emit_error;

int
xv6_tcc_emit_begin(Section *section)
{
  if(section == 0)
    return -1;

  if(section->sh_type == SHT_NOBITS)
    return -1;

  current_section = section;
  current_offset = section->data_offset;
  code_generation_disabled = 0;
  emit_error = 0;

  return 0;
}

void
xv6_tcc_emit_set_disabled(int disabled)
{
  code_generation_disabled = disabled != 0;
}

void
xv6_tcc_g(int value)
{
  unsigned long next_offset;

  if(code_generation_disabled || emit_error)
    return;

  if(current_section == 0){
    emit_error = -1;
    return;
  }

  if(current_offset == 0x7fffffffUL){
    emit_error = -1;
    return;
  }

  next_offset = current_offset + 1;

  if(next_offset > current_section->data_allocated){
    if(xv6_tcc_section_realloc(current_section,
                               next_offset) < 0){
      emit_error = -1;
      return;
    }
  }

  current_section->data[current_offset] =
    (unsigned char)value;

  current_offset = next_offset;
  current_section->data_offset = current_offset;
}

void
xv6_tcc_gen_le16(int value)
{
  /*Se conserva el mismo orden de emisión utilizado por
  gen_le16() en el backend original de TinyCC*/

  xv6_tcc_g(value);
  xv6_tcc_g(value >> 8);
}

void
xv6_tcc_gen_le32(int value)
{
  unsigned long next_offset;

  if(code_generation_disabled || emit_error)
    return;

  if(current_section == 0){
    emit_error = -1;
    return;
  }

  if(current_offset > 0x7ffffffbUL){
    emit_error = -1;
    return;
  }

  next_offset = current_offset + 4;

  if(next_offset > current_section->data_allocated){
    if(xv6_tcc_section_realloc(current_section,
                               next_offset) < 0){
      emit_error = -1;
      return;
    }
  }

  /*Se conserva la escritura little-endian utilizada por
  gen_le32() en el backend RISC-V original*/

  current_section->data[current_offset++] =
    value & 0xff;

  current_section->data[current_offset++] =
    (value >> 8) & 0xff;

  current_section->data[current_offset++] =
    (value >> 16) & 0xff;

  current_section->data[current_offset++] =
    (value >> 24) & 0xff;

  current_section->data_offset = current_offset;
}

int
xv6_tcc_emit_status(void)
{
  return emit_error;
}

int
xv6_tcc_check_emitter(void)
{
  Section *text;

  text = xv6_tcc_new_section(
    ".text",
    SHT_PROGBITS,
    SHF_ALLOC | SHF_EXECINSTR
  );

  if(text == 0)
    return -1;

  if(xv6_tcc_emit_begin(text) < 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  /*Se emite addi a0, zero, 42.
  La codificación completa es 0x02a00513*/

  xv6_tcc_gen_le32(0x02a00513);

  /*Se emite ret, pseudoinstrucción equivalente a
  jalr zero, 0(ra), con codificación 0x00008067*/

  xv6_tcc_gen_le32(0x00008067);

  if(xv6_tcc_emit_status() < 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  if(text->data_offset != 8){
    xv6_tcc_delete_section(text);
    return -1;
  }

  /*Se comprueba el orden little-endian de las dos
  instrucciones almacenadas dentro de .text*/

  if(text->data[0] != 0x13 ||
     text->data[1] != 0x05 ||
     text->data[2] != 0xa0 ||
     text->data[3] != 0x02 ||
     text->data[4] != 0x67 ||
     text->data[5] != 0x80 ||
     text->data[6] != 0x00 ||
     text->data[7] != 0x00){
    xv6_tcc_delete_section(text);
    return -1;
  }

   // Se muestran los bytes emitidos para comprobarlos desde QEMU
  printf("asxv6: prueba .text: ");

  for(int i = 0; i < 8; i++)
    printf("%x ", text->data[i]);

  printf("\n");

  xv6_tcc_delete_section(text);

  return 0;
}