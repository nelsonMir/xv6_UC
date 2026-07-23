#include "kernel/types.h"
#include "user/user.h"

#include "xv6_alloc.h"
#include "xv6_section.h"

#define XV6_TCC_MAX_SECTION_SIZE 0x7fffffffUL
#define XV6_TCC_POINTER_SIZE     8

Section *
xv6_tcc_new_section(const char *name,
                    int sh_type,
                    int sh_flags)
{
  Section *section;
  unsigned long allocation_size;
  int name_length;

  if(name == 0)
    return 0;

  name_length = strlen(name);

  if(name_length < 0)
    return 0;

  /*La estructura ya contiene un byte para name, por lo que
  se añaden exactamente los bytes restantes del nombre*/

  allocation_size = sizeof(Section) +
                    (unsigned long)name_length;

  if(allocation_size > XV6_TCC_MAX_SECTION_SIZE)
    return 0;

  section = xv6_tcc_realloc(0, allocation_size);
  if(section == 0)
    return 0;

  memset(section, 0, (int)allocation_size);
  memmove(section->name, name, name_length + 1);

  section->sh_type = sh_type;
  section->sh_flags = sh_flags;

  /*Se utiliza la alineación predeterminada de TinyCC para RV64.
  Las tablas de cadenas utilizan alineación de un byte*/

  if(sh_type == SHT_STRTAB)
    section->sh_addralign = 1;
  else
    section->sh_addralign = XV6_TCC_POINTER_SIZE;

  return section;
}

void
xv6_tcc_delete_section(Section *section)
{
  if(section == 0)
    return;

  if(section->data != 0)
    xv6_tcc_realloc(section->data, 0);

  xv6_tcc_realloc(section, 0);
}

int
xv6_tcc_section_realloc(Section *section,
                        unsigned long new_size)
{
  unsigned char *new_data;
  unsigned long allocated_size;
  unsigned long old_size;

  if(section == 0)
    return -1;

  if(new_size <= section->data_allocated)
    return 0;

  if(new_size > XV6_TCC_MAX_SECTION_SIZE)
    return -1;

  old_size = section->data_allocated;
  allocated_size = old_size;

  if(allocated_size == 0)
    allocated_size = 1;

  /*Se duplica el tamaño reservado hasta alcanzar el solicitado,
  siguiendo el crecimiento utilizado por TinyCC*/

  while(allocated_size < new_size){
    if(allocated_size >
       XV6_TCC_MAX_SECTION_SIZE / 2){
      allocated_size = new_size;
      break;
    }

    allocated_size *= 2;
  }

  new_data = xv6_tcc_realloc(section->data,
                             allocated_size);

  if(new_data == 0)
    return -1;

  /*Se inicializa a cero la zona recién reservada porque las
  secciones de TinyCC esperan este comportamiento*/

  memset(new_data + old_size,
         0,
         (int)(allocated_size - old_size));

  section->data = new_data;
  section->data_allocated = allocated_size;

  return 0;
}

int
xv6_tcc_section_add(Section *section,
                    unsigned long size,
                    int align,
                    unsigned long *offset_result)
{
  unsigned long offset;
  unsigned long end_offset;
  unsigned long alignment;

  if(section == 0 || offset_result == 0)
    return -1;

  if(align <= 0)
    return -1;

  // Se exige una alineación que sea potencia de dos
  if((align & (align - 1)) != 0)
    return -1;

  alignment = (unsigned long)align;

  if(section->data_offset >
     XV6_TCC_MAX_SECTION_SIZE - (alignment - 1))
    return -1;

  offset = (section->data_offset + alignment - 1) &
           ~(alignment - 1);

  if(size > XV6_TCC_MAX_SECTION_SIZE - offset)
    return -1;

  end_offset = offset + size;

  /*Las secciones NOBITS, como .bss, aumentan su tamaño lógico
  sin reservar contenido dentro del archivo*/

  if(section->sh_type != SHT_NOBITS &&
     end_offset > section->data_allocated){
    if(xv6_tcc_section_realloc(section,
                               end_offset) < 0)
      return -1;
  }

  section->data_offset = end_offset;

  if(align > section->sh_addralign)
    section->sh_addralign = align;

  *offset_result = offset;

  return 0;
}

void *
xv6_tcc_section_ptr_add(Section *section,
                        unsigned long size)
{
  unsigned long offset;

  if(xv6_tcc_section_add(section,
                         size,
                         1,
                         &offset) < 0)
    return 0;

  if(section->sh_type == SHT_NOBITS)
    return 0;

  return section->data + offset;
}

int
xv6_tcc_check_section_model(void)
{
  Section *text;
  unsigned char *first_data;
  unsigned char *aligned_data;
  unsigned long aligned_offset;

  text = xv6_tcc_new_section(".text",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_EXECINSTR);

  if(text == 0)
    return -1;

  if(strcmp(text->name, ".text") != 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  if(text->sh_type != SHT_PROGBITS){
    xv6_tcc_delete_section(text);
    return -1;
  }

  if((text->sh_flags & SHF_EXECINSTR) == 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  /*Se reservan tres bytes para comprobar el crecimiento inicial
  y la actualización de data_offset*/

  first_data = xv6_tcc_section_ptr_add(text, 3);
  if(first_data == 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  first_data[0] = 0x11;
  first_data[1] = 0x22;
  first_data[2] = 0x33;

  if(text->data_offset != 3){
    xv6_tcc_delete_section(text);
    return -1;
  }

  /*Se añade un bloque alineado a cuatro bytes. Debe quedar un
  byte de relleno entre ambos bloques*/

  if(xv6_tcc_section_add(text,
                         4,
                         4,
                         &aligned_offset) < 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  if(aligned_offset != 4 ||
     text->data_offset != 8){
    xv6_tcc_delete_section(text);
    return -1;
  }

  if(text->data[3] != 0){
    xv6_tcc_delete_section(text);
    return -1;
  }

  aligned_data = text->data + aligned_offset;
  aligned_data[0] = 0x44;
  aligned_data[1] = 0x55;
  aligned_data[2] = 0x66;
  aligned_data[3] = 0x77;

  if(text->data[0] != 0x11 ||
     text->data[1] != 0x22 ||
     text->data[2] != 0x33 ||
     text->data[4] != 0x44 ||
     text->data[7] != 0x77){
    xv6_tcc_delete_section(text);
    return -1;
  }

  xv6_tcc_delete_section(text);

  return 0;
}