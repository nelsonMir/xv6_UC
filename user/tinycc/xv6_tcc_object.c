/*
xv6_tcc_object.c

Es una reprentación del fichero objeto a generar pero en memoria, todavía no genera el ELF 
en el disco. 
Construccion educativa de simbolos y relocaciones.
La organizacion sigue el modelo de TinyCC tccelf.c y riscv64-asm.c, pero
sustituye sus estados dinamicos y callbacks por arrays de capacidad fija
adecuados para xv6.
Este fichero recibirá las líneas de código ya separadas por el parser de líneas 
y con ellas va construyendo las estructuras del ELF en memoria, todavía no se 
escriben en almacenamiento persistente.
En resumen es un estado educativo de un objeto ELF relocatable mientras se ensambla.
Mantiene el codigo de .text, los simbolos descubiertos y las relocaciones
pendientes. De momento todo se construye en memoria, luego me encargaré 
de escribir el fichero ELF64 ET_REL completo.

Donante conceptual:
  TinyCC tccelf.c y riscv64-asm.c

*/

/*
He ampliado el sistesma para la construcción del objeto relocatable, 
para agregar simbolos debiles, .rela.rodata,
.rela.data y referencias simbolicas de 32/64 bits en datos.

*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_object.h"

/*Función auxiliar que busca un símbolo por su nombre en el array temporal 
de símbolos dentro de la estructura temporal de construcción del fichero objeto.
Se devuelve la posición interna dentro del array temporal */
static int
symbol_slot_by_name(const struct Xv6TccObjectBuilder *object,
                    const char *name)
{
  int i;

  //válido que exista el objeto y el nombre del símbolo
  if(!object || !name)
    return -1;
  //recorro los símbolos
  for(i = 0; i < object->symbol_count; i++)
    if(strcmp(object->symbols[i].name, name) == 0)
      return i; //deevuelvo la posición interna
  return -1;
}

static int
constant_slot_by_name(const struct Xv6TccObjectBuilder *object,
                      const char *name)
{
  int i;

  if(!object || !name)
    return -1;
  for(i = 0; i < object->constant_count; i++)
    if(strcmp(object->constants[i].name, name) == 0)
      return i;
  return -1;
}

/*Crea un nuevo símbolo  en el array temporal 
de símbolos dentro de la estructura temporal de construcción del fichero objeto.
Esta función no define el símbolo (definir su refencia) porque no todos los símbolos podrían estar definidos en este fichero objeto
sino que podrían estar definidos en otro fichero*/
static int
create_symbol(struct Xv6TccObjectBuilder *object,
              const char *name, int *slot)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int length;

  if(!object || !name || !slot || !xv6_tcc_valid_identifier(name) ||
     constant_slot_by_name(object, name) >= 0)
    return -1;
  //como límite he puesto que el máximo número de símbolos será 32
  if(object->symbol_count >= XV6_TCC_OBJECT_MAX_SYMBOLS)
    return -1;

  //Verifico que el símbolo a meter no tenga un nombre mayor ya XV6_TCC_LINE_NAME_MAX
  length = strlen(name);
  if(length <= 0 || length >= (int)sizeof(object->symbols[0].name))
    return -1;

  //busco el siguiente slot libre en el array de símbolos auxiliar para meter el nuevo
  *slot = object->symbol_count;
  //obtengo al dirección en el array de símbolos para meter el nuevo valor
  symbol = &object->symbols[object->symbol_count];
  //aumento el contador
  object->symbol_count++;
  /*POngo todos los campos el símbolo a cero:
  value = 0
  size = 0
  defined = 0
  elf_index = 0
  ...*/
  memset(symbol, 0, sizeof(*symbol));
  //copio el nombre del nuevo símbolo con el nulo
  memmove(symbol->name, name, length + 1);
  //pongo el estado inicial del símbolo: símbolo local y sin definir su referencia, ya que no se sabe si aparecerá .global o si se definirá en este fichero
  symbol->binding = XV6_TCC_STB_LOCAL;
  symbol->section_index = XV6_TCC_SHN_UNDEF;
  return 0;
}

/*Busca un símbolo y si no existe lo crea.

Esto sirve para cuando tengamos símbolos con referencias adelantadas, es decir, aparece una etiqueta para un salto pero todavía no sabemos 
a dónde va hasta que aparezca con los ":"
Ej:
beq a0, zero, done
...
done:

y devuelvo el slot que ocupa en el array temporal de símbolos*/
static int
get_or_create_symbol(struct Xv6TccObjectBuilder *object,
                     const char *name, int *slot)
{
  int found;

  //si existe el símbolo, devuelvo el slot que ocupa en el array temporal de símbolos
  found = symbol_slot_by_name(object, name);
  if(found >= 0){
    *slot = found;
    return 0;
  }
  //si no existe, creo el símbolo
  return create_symbol(object, name, slot);
}

static struct Xv6TccElfBuffer *
current_buffer(struct Xv6TccObjectBuilder *object)
{
  if(!object)
    return 0;
  if(object->current_section == XV6_TCC_SHN_TEXT)
    return object->text;
  if(object->current_section == XV6_TCC_SHN_RODATA)
    return object->rodata;
  if(object->current_section == XV6_TCC_SHN_DATA)
    return object->data;
  return 0;
}

static uint64
current_size(const struct Xv6TccObjectBuilder *object)
{
  if(object->current_section == XV6_TCC_SHN_TEXT)
    return object->text->size;
  if(object->current_section == XV6_TCC_SHN_RODATA)
    return object->rodata->size;
  if(object->current_section == XV6_TCC_SHN_DATA)
    return object->data->size;
  if(object->current_section == XV6_TCC_SHN_BSS)
    return object->bss_size;
  return 0;
}

static void
update_alignment(struct Xv6TccObjectBuilder *object, uint align)
{
  uint *current;

  current = 0;
  if(object->current_section == XV6_TCC_SHN_TEXT)
    current = &object->text_align;
  else if(object->current_section == XV6_TCC_SHN_RODATA)
    current = &object->rodata_align;
  else if(object->current_section == XV6_TCC_SHN_DATA)
    current = &object->data_align;
  else if(object->current_section == XV6_TCC_SHN_BSS)
    current = &object->bss_align;

  if(current && align > *current)
    *current = align;
}

static int
define_label(struct Xv6TccObjectBuilder *object, const char *name)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int slot;

  //obtiene o crear el símbolo en el array de símbolos temporal y devuelve su posición en el array
  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;

  /*se obtiene el símbolo del array. SI el símbolo ya está definido es un error, ósea se ha detectado 
  una definición duplicada. EJ:
  loop:
    addi a0, a0, 1

  loop:
      ret
      
  COmo hay definición duplicada, se rechaza*/
  symbol = &object->symbols[slot];
  if(symbol->defined)
    return -1;

  //Se define el símbolo
  symbol->defined = 1;
  symbol->section_index = object->current_section;
  symbol->value = current_size(object);
  symbol->size = 0;
  return 0;
}

/*Marca un símbolo como visible globalmente
Esto se usa para: 
.globl main
main:

Como main aparece antes, se pone por defecto como local*/
static int
mark_global(struct Xv6TccObjectBuilder *object, const char *name)
{
  int slot;

  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;
  object->symbols[slot].binding = XV6_TCC_STB_GLOBAL;
  return 0;
}

static int
mark_weak(struct Xv6TccObjectBuilder *object, const char *name)
{
  int slot;

  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;
  object->symbols[slot].binding = XV6_TCC_STB_WEAK;
  return 0;
}

static int
mark_local(struct Xv6TccObjectBuilder *object, const char *name)
{
  int slot;

  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;
  object->symbols[slot].binding = XV6_TCC_STB_LOCAL;
  return 0;
}

static int
parse_value(const struct Xv6TccObjectBuilder *object,
            const char *text, long *value)
{
  int slot;

  if(!object || !text || !value)
    return -1;
  slot = constant_slot_by_name(object, text);
  if(slot >= 0){
    *value = object->constants[slot].value;
    return 0;
  }
  return xv6_tcc_parse_integer(text, value);
}

static int
space_character(int character)
{
  return character == ' ' || character == '\t' ||
         character == '\r' || character == '\n';
}

/*
Interpreta los operandos de datos como:
  numero o constante
  simbolo
  simbolo + numero
  simbolo - numero
*/
static int
parse_data_expression(struct Xv6TccObjectBuilder *object,
                      const char *text, int *is_symbol,
                      long *value, int *symbol_slot, long *addend)
{
  char name[XV6_TCC_LINE_NAME_MAX];
  const char *cursor;
  int length;
  int sign;
  long amount;

  if(!object || !text || !is_symbol || !value ||
     !symbol_slot || !addend)
    return -1;

  if(parse_value(object, text, value) == 0){
    *is_symbol = 0;
    *symbol_slot = -1;
    *addend = 0;
    return 0;
  }

  cursor = text;
  while(space_character(*cursor))
    cursor++;
  length = 0;
  while(cursor[length] && !space_character(cursor[length]) &&
        cursor[length] != '+' && cursor[length] != '-'){
    if(length >= (int)sizeof(name) - 1)
      return -1;
    name[length] = cursor[length];
    length++;
  }
  if(length == 0)
    return -1;
  name[length] = 0;
  if(!xv6_tcc_valid_identifier(name) ||
     get_or_create_symbol(object, name, symbol_slot) < 0)
    return -1;

  cursor += length;
  while(space_character(*cursor))
    cursor++;
  if(*cursor == 0){
    *is_symbol = 1;
    *value = 0;
    *addend = 0;
    return 0;
  }

  if(*cursor != '+' && *cursor != '-')
    return -1;
  sign = *cursor == '-' ? -1 : 1;
  cursor++;
  while(space_character(*cursor))
    cursor++;
  if(*cursor == 0 || parse_value(object, cursor, &amount) < 0 || amount < 0)
    return -1;

  *is_symbol = 1;
  *value = 0;
  *addend = sign < 0 ? -amount : amount;
  return 0;
}

static int
long_to_text(long value, char *buffer, int capacity)
{
  char reverse[32];
  unsigned long magnitude;
  int negative;
  int count;
  int out;

  if(!buffer || capacity < 2)
    return -1;

  negative = value < 0;
  if(negative)
    magnitude = (unsigned long)(-(value + 1)) + 1;
  else
    magnitude = (unsigned long)value;

  count = 0;
  do {
    reverse[count++] = '0' + magnitude % 10;
    magnitude /= 10;
  } while(magnitude && count < (int)sizeof(reverse));

  out = 0;
  if(negative){
    if(out >= capacity - 1)
      return -1;
    buffer[out++] = '-';
  }
  while(count > 0){
    if(out >= capacity - 1)
      return -1;
    buffer[out++] = reverse[--count];
  }
  buffer[out] = 0;
  return 0;
}

static int
resolve_constant_operands(const struct Xv6TccObjectBuilder *object,
                          const struct Xv6TccParsedLine *line,
                          struct Xv6TccParsedLine *resolved)
{
  int i;

  if(!object || !line || !resolved)
    return -1;
  memmove(resolved, line, sizeof(*resolved));

  for(i = 0; i < resolved->operand_count; i++){
    int slot;

    slot = constant_slot_by_name(object, resolved->operands[i]);
    if(slot >= 0 && long_to_text(object->constants[slot].value,
                                resolved->operands[i],
                                sizeof(resolved->operands[i])) < 0)
      return -1;
  }
  return 0;
}


static int
generated_symbol_name(struct Xv6TccObjectBuilder *object,
                      char *name, int capacity)
{
  char number[32];
  const char *prefix;
  int prefix_length;
  int number_length;

  if(!object || !name || capacity <= 0 ||
     long_to_text(object->generated_symbol_count,
                  number, sizeof(number)) < 0)
    return -1;

  prefix = ".Lpcrel";
  prefix_length = strlen(prefix);
  number_length = strlen(number);
  if(prefix_length + number_length >= capacity)
    return -1;

  memmove(name, prefix, prefix_length);
  memmove(name + prefix_length, number, number_length + 1);
  object->generated_symbol_count++;
  return 0;
}

static int
append_relocation(struct Xv6TccObjectBuilder *object,
                  uint offset, uint type, int symbol_slot, long addend)
{
  struct Xv6TccAssemblerRelocation *relocation;

  if(!object || symbol_slot < 0 ||
     object->relocation_count >= XV6_TCC_OBJECT_MAX_RELOCATIONS)
    return -1;
  relocation = &object->relocations[object->relocation_count];
  object->relocation_count++;
  relocation->section_index = object->current_section;
  relocation->offset = offset;
  relocation->type = type;
  relocation->symbol_slot = symbol_slot;
  relocation->addend = addend;
  return 0;
}

static int
emit_symbolic_instruction(struct Xv6TccObjectBuilder *object,
                          const struct Xv6TccParsedLine *line,
                          const struct Xv6TccInstruction *instruction)
{
  const char *symbol_name;
  const char *operand1;
  const char *operand2;
  const char *operand3;
  uint relocation_type;
  uint word;
  uint offset;
  int symbol_slot;

  if(object->current_section != XV6_TCC_SHN_TEXT)
    return -1;

  //Primero recupero los operandos de la instrucción y los meto en variables auxiliares
  //si la instrucción no tiene los 3 operandos por su formato, se pone 0
  operand1 = line->operand_count > 0 ? line->operands[0] : 0;
  operand2 = line->operand_count > 1 ? line->operands[1] : 0;
  operand3 = line->operand_count > 2 ? line->operands[2] : 0;

  if(instruction->kind == XV6_TCC_PSEUDO_CALL){
    if(!operand1 || !xv6_tcc_valid_identifier(operand1) ||
       get_or_create_symbol(object, operand1, &symbol_slot) < 0 ||
       xv6_tcc_emit32(object->text, 0x00000097U, &offset) < 0 ||
       xv6_tcc_emit32(object->text, 0x000080e7U, &word) < 0 ||
       append_relocation(object, offset, XV6_TCC_R_RISCV_CALL,
                         symbol_slot, 0) < 0)
      return -1;
    update_alignment(object, 4);
    return 0;
  }

  if(instruction->kind == XV6_TCC_PSEUDO_LA){
    char anchor[XV6_TCC_LINE_NAME_MAX];
    int rd;
    int anchor_slot;
    uint high_offset;
    uint low_offset;

    if(!operand1 || !operand2 ||
       xv6_tcc_parse_register(operand1, &rd) < 0 ||
       !xv6_tcc_valid_identifier(operand2) ||
       get_or_create_symbol(object, operand2, &symbol_slot) < 0 ||
       generated_symbol_name(object, anchor, sizeof(anchor)) < 0 ||
       define_label(object, anchor) < 0 ||
       symbol_slot_by_name(object, anchor) < 0)
      return -1;
    anchor_slot = symbol_slot_by_name(object, anchor);

    if(xv6_tcc_encode_u(0x17, rd, 0, &word) < 0 ||
       xv6_tcc_emit32(object->text, word, &high_offset) < 0 ||
       xv6_tcc_encode_i(0x13, 0, rd, rd, 0, &word) < 0 ||
       xv6_tcc_emit32(object->text, word, &low_offset) < 0 ||
       append_relocation(object, high_offset,
                         XV6_TCC_R_RISCV_PCREL_HI20,
                         symbol_slot, 0) < 0 ||
       append_relocation(object, low_offset,
                         XV6_TCC_R_RISCV_PCREL_LO12_I,
                         anchor_slot, 0) < 0)
      return -1;
    update_alignment(object, 4);
    return 0;
  }

  //todavía no hemos determinado el nombre del símbolo ni el tipo de relocación
  symbol_name = 0;
  relocation_type = 0;
  if(instruction->kind == XV6_TCC_INSN_BRANCH && operand3 &&
     xv6_tcc_valid_identifier(operand3)){
    //guardo el nombre del símbolo
    symbol_name = operand3;
    //provisionalmente se codificará el desplazamiento de ese símbolo a 0 Ej: beq a0, zero, 0
    operand3 = "0";
    //y por último le indico al linker que deberá resolver esto aplicando una reloación branch "relocation_type = XV6_TCC_R_RISCV_BRANCH"
    relocation_type = XV6_TCC_R_RISCV_BRANCH;
  } else if(instruction->kind == XV6_TCC_INSN_JAL && operand2 &&
            xv6_tcc_valid_identifier(operand2)){
            //guardo el nombre del símbolo
    symbol_name = operand2;
            //provisionalmente se codificará el desplazamiento de ese símbolo a 0 Ej: jal ra, 0
    operand2 = "0";
    relocation_type = XV6_TCC_R_RISCV_JAL;
  } else if(instruction->kind == XV6_TCC_PSEUDO_J && operand1 &&
            xv6_tcc_valid_identifier(operand1)){
    symbol_name = operand1;
              //provisionalmente se codificará el desplazamiento de ese símbolo a 0 
    operand1 = "0";
              //y por último le indico al linker que deberá resolver esto aplicando una reloación branch "relocation_type = XV6_TCC_R_RISCV_JAL"
    relocation_type = XV6_TCC_R_RISCV_JAL;
  } else {
    return -1;
  }

  if(get_or_create_symbol(object, symbol_name, &symbol_slot) < 0 ||
     xv6_tcc_encode_named_instruction(line->name,
                                       operand1, operand2, operand3,
                                       &word) < 0 ||
     xv6_tcc_emit32(object->text, word, &offset) < 0 ||
     append_relocation(object, offset, relocation_type,
                       symbol_slot, 0) < 0)
    return -1;

  update_alignment(object, 4);
  return 0;
}

static int
emit_instruction(struct Xv6TccObjectBuilder *object,
                 const struct Xv6TccParsedLine *line)
{
  const struct Xv6TccInstruction *instruction;
  struct Xv6TccParsedLine resolved;
  uint offset;

  if(object->current_section != XV6_TCC_SHN_TEXT ||
     resolve_constant_operands(object, line, &resolved) < 0)
    return -1;

  if(xv6_tcc_emit_parsed_instruction(&resolved, object->text, &offset) == 0){
    update_alignment(object, 4);
    return 0;
  }

  instruction = xv6_tcc_find_instruction(resolved.name);
  if(!instruction)
    return -1;

  if(instruction->kind == XV6_TCC_PSEUDO_LI){
    int rd;
    long value;
    long high;
    long low;
    uint word;

    if(xv6_tcc_parse_register(resolved.operands[0], &rd) < 0 ||
       xv6_tcc_parse_integer(resolved.operands[1], &value) < 0 ||
       value < -2147483648L || value > 2147483647L)
      return -1;
    high = (value + 0x800L) >> 12;
    low = value - (high << 12);
    if(xv6_tcc_encode_u(0x37, rd, (uint)high & 0xfffffU, &word) < 0 ||
       xv6_tcc_emit32(object->text, word, &offset) < 0 ||
       xv6_tcc_encode_i(0x13, 0, rd, rd, low, &word) < 0 ||
       xv6_tcc_emit32(object->text, word, &offset) < 0)
      return -1;
    update_alignment(object, 4);
    return 0;
  }

  return emit_symbolic_instruction(object, &resolved, instruction);
}

static int
select_section(struct Xv6TccObjectBuilder *object, const char *name)
{
  if(strcmp(name, ".text") == 0)
    object->current_section = XV6_TCC_SHN_TEXT;
  else if(strcmp(name, ".rodata") == 0)
    object->current_section = XV6_TCC_SHN_RODATA;
  else if(strcmp(name, ".data") == 0)
    object->current_section = XV6_TCC_SHN_DATA;
  else if(strcmp(name, ".bss") == 0)
    object->current_section = XV6_TCC_SHN_BSS;
  else
    return -1;
  return 0;
}

static int
put_constant(struct Xv6TccObjectBuilder *object,
             const char *name, const char *value_text, int replace)
{
  struct Xv6TccAssemblerConstant *constant;
  long value;
  int slot;
  int length;

  if(!xv6_tcc_valid_identifier(name) ||
     symbol_slot_by_name(object, name) >= 0 ||
     parse_value(object, value_text, &value) < 0)
    return -1;

  slot = constant_slot_by_name(object, name);
  if(slot >= 0){
    if(!replace)
      return -1;
    object->constants[slot].value = value;
    return 0;
  }

  if(object->constant_count >= XV6_TCC_OBJECT_MAX_CONSTANTS)
    return -1;
  length = strlen(name);
  if(length <= 0 || length >= (int)sizeof(object->constants[0].name))
    return -1;

  constant = &object->constants[object->constant_count];
  object->constant_count++;
  memset(constant, 0, sizeof(*constant));
  memmove(constant->name, name, length + 1);
  constant->value = value;
  return 0;
}

static int
fits_width(long value, int bytes)
{
  if(bytes == 1)
    return value >= -128 && value <= 255;
  if(bytes == 2)
    return value >= -32768 && value <= 65535;
  if(bytes == 4)
    return value >= -(1L << 31) && value <= 0xffffffffL;
  return bytes == 8;
}

static int
emit_integer(struct Xv6TccObjectBuilder *object,
             long value, int bytes)
{
  struct Xv6TccElfBuffer *section;
  uint offset;
  uint64 encoded;
  int i;

  if(object->current_section == XV6_TCC_SHN_BSS ||
     object->current_section == XV6_TCC_SHN_TEXT ||
     !fits_width(value, bytes))
    return -1;

  section = current_buffer(object);
  if(!section || xv6_tcc_section_add(section, bytes, bytes, &offset) < 0)
    return -1;

  encoded = (uint64)value;
  for(i = 0; i < bytes; i++)
    section->data[offset + i] = (uchar)(encoded >> (8 * i));
  update_alignment(object, bytes);
  return 0;
}

static int
emit_symbol_data(struct Xv6TccObjectBuilder *object,
                 int symbol_slot, long addend, int bytes)
{
  struct Xv6TccElfBuffer *section;
  uint offset;
  uint type;

  if(!object || symbol_slot < 0 ||
     (object->current_section != XV6_TCC_SHN_RODATA &&
      object->current_section != XV6_TCC_SHN_DATA) ||
     (bytes != 4 && bytes != 8))
    return -1;

  section = current_buffer(object);
  if(!section || xv6_tcc_section_add(section, bytes, bytes, &offset) < 0)
    return -1;
  memset(section->data + offset, 0, bytes);
  type = bytes == 4 ? XV6_TCC_R_RISCV_32 : XV6_TCC_R_RISCV_64;
  if(append_relocation(object, offset, type, symbol_slot, addend) < 0)
    return -1;
  update_alignment(object, bytes);
  return 0;
}

static int
hex_digit(int c)
{
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int
decode_string(const char *text, uchar *output,
              uint capacity, uint *length)
{
  uint input;
  uint out;
  int text_length;

  if(!text || !output || !length)
    return -1;
  text_length = strlen(text);
  if(text_length < 2 || text[0] != '"' || text[text_length - 1] != '"')
    return -1;

  input = 1;
  out = 0;
  while(input < (uint)text_length - 1){
    int character;

    character = (uchar)text[input++];
    if(character == '\\'){
      int escaped;

      if(input >= (uint)text_length - 1)
        return -1;
      escaped = (uchar)text[input++];
      if(escaped == 'n')
        character = '\n';
      else if(escaped == 'r')
        character = '\r';
      else if(escaped == 't')
        character = '\t';
      else if(escaped == '0')
        character = 0;
      else if(escaped == '\\' || escaped == '"')
        character = escaped;
      else if(escaped == 'x'){
        int high;
        int low;

        if(input + 1 >= (uint)text_length)
          return -1;
        high = hex_digit((uchar)text[input++]);
        low = hex_digit((uchar)text[input++]);
        if(high < 0 || low < 0)
          return -1;
        character = (high << 4) | low;
      } else {
        return -1;
      }
    }

    if(out >= capacity)
      return -1;
    output[out++] = character;
  }

  *length = out;
  return 0;
}

static int
emit_string(struct Xv6TccObjectBuilder *object,
            const char *text, int terminate)
{
  uchar decoded[XV6_TCC_LINE_OPERAND_MAX];
  struct Xv6TccElfBuffer *section;
  uint length;
  uint offset;

  if(object->current_section == XV6_TCC_SHN_TEXT ||
     object->current_section == XV6_TCC_SHN_BSS ||
     decode_string(text, decoded, sizeof(decoded) - terminate, &length) < 0)
    return -1;

  section = current_buffer(object);
  if(!section || xv6_tcc_section_add(section, length + terminate,
                                     1, &offset) < 0)
    return -1;
  if(length)
    memmove(section->data + offset, decoded, length);
  if(terminate)
    section->data[offset + length] = 0;
  return 0;
}

static int
emit_space(struct Xv6TccObjectBuilder *object,
           long count, long fill)
{
  struct Xv6TccElfBuffer *section;
  uint offset;

  if(count < 0 || (uint64)count > 0xffffffffULL || fill < 0 || fill > 255)
    return -1;

  if(object->current_section == XV6_TCC_SHN_BSS){
    if(fill != 0 || (uint64)count > 0xffffffffffffffffULL - object->bss_size)
      return -1;
    object->bss_size += count;
    return 0;
  }
  if(object->current_section == XV6_TCC_SHN_TEXT)
    return -1;

  section = current_buffer(object);
  if(!section || xv6_tcc_section_add(section, count, 1, &offset) < 0)
    return -1;
  if(count)
    memset(section->data + offset, fill, count);
  return 0;
}

static int
align_current_section(struct Xv6TccObjectBuilder *object, uint align)
{
  struct Xv6TccElfBuffer *section;
  uint offset;
  uint64 aligned;

  if(align == 0 || (align & (align - 1)) != 0)
    return -1;

  if(object->current_section == XV6_TCC_SHN_BSS){
    aligned = (object->bss_size + align - 1) & ~((uint64)align - 1);
    object->bss_size = aligned;
  } else {
    section = current_buffer(object);
    if(!section || xv6_tcc_section_add(section, 0, align, &offset) < 0)
      return -1;
  }
  update_alignment(object, align);
  return 0;
}

static int
process_data_directive(struct Xv6TccObjectBuilder *object,
                       const struct Xv6TccParsedLine *line,
                       int bytes)
{
  int i;

  for(i = 0; i < line->operand_count; i++){
    int is_symbol;
    int symbol_slot;
    long value;
    long addend;

    if(parse_data_expression(object, line->operands[i],
                             &is_symbol, &value,
                             &symbol_slot, &addend) < 0)
      return -1;
    if(is_symbol){
      if(emit_symbol_data(object, symbol_slot, addend, bytes) < 0)
        return -1;
    } else if(emit_integer(object, value, bytes) < 0){
      return -1;
    }
  }
  return 0;
}

static int
process_directive(struct Xv6TccObjectBuilder *object,
                  const struct Xv6TccParsedLine *line)
{
  int i;

  if(strcmp(line->name, ".text") == 0 ||
     strcmp(line->name, ".rodata") == 0 ||
     strcmp(line->name, ".data") == 0 ||
     strcmp(line->name, ".bss") == 0)
    return select_section(object, line->name);

  if(strcmp(line->name, ".section") == 0)
    return line->operand_count >= 1 ?
        select_section(object, line->operands[0]) : -1;

  //.globl y .global significan lo mismo. Aquí se marcan como globales todos los símbolos en la misma línea de la directiva .global EJ: .globl main, funcion
  //tanto "main" como "funcion" se marcan como globales
  if(strcmp(line->name, ".globl") == 0 ||
     strcmp(line->name, ".global") == 0 ||
     strcmp(line->name, ".weak") == 0 ||
     strcmp(line->name, ".local") == 0){
    for(i = 0; i < line->operand_count; i++){
      int status;

      if(strcmp(line->name, ".weak") == 0)
        status = mark_weak(object, line->operands[i]);
      else if(strcmp(line->name, ".local") == 0)
        status = mark_local(object, line->operands[i]);
      else
        status = mark_global(object, line->operands[i]);
      if(status < 0)
        return -1;
    }
    return 0;
  }

  if(strcmp(line->name, ".equ") == 0)
    return put_constant(object, line->operands[0], line->operands[1], 0);
  if(strcmp(line->name, ".set") == 0)
    return put_constant(object, line->operands[0], line->operands[1], 1);

  if(strcmp(line->name, ".byte") == 0)
    return process_data_directive(object, line, 1);
  if(strcmp(line->name, ".2byte") == 0 ||
     strcmp(line->name, ".half") == 0)
    return process_data_directive(object, line, 2);
  if(strcmp(line->name, ".4byte") == 0 ||
     strcmp(line->name, ".word") == 0)
    return process_data_directive(object, line, 4);
  if(strcmp(line->name, ".8byte") == 0 ||
     strcmp(line->name, ".dword") == 0)
    return process_data_directive(object, line, 8);

  if(strcmp(line->name, ".ascii") == 0 ||
     strcmp(line->name, ".asciz") == 0 ||
     strcmp(line->name, ".string") == 0){
    int terminate;

    terminate = strcmp(line->name, ".ascii") != 0;
    for(i = 0; i < line->operand_count; i++)
      if(emit_string(object, line->operands[i], terminate) < 0)
        return -1;
    return 0;
  }

  if(strcmp(line->name, ".zero") == 0 ||
     strcmp(line->name, ".space") == 0 ||
     strcmp(line->name, ".skip") == 0){
    long count;
    long fill;

    fill = 0;
    if(parse_value(object, line->operands[0], &count) < 0 ||
       (line->operand_count > 1 &&
        parse_value(object, line->operands[1], &fill) < 0))
      return -1;
    return emit_space(object, count, fill);
  }

  if(strcmp(line->name, ".align") == 0 ||
     strcmp(line->name, ".p2align") == 0 ||
     strcmp(line->name, ".balign") == 0){
    long value;
    long fill;
    uint align;

    fill = 0;
    if(line->operand_count > 2 ||
       parse_value(object, line->operands[0], &value) < 0 || value < 0 ||
       (line->operand_count > 1 &&
        parse_value(object, line->operands[1], &fill) < 0) || fill != 0)
      return -1;

    if(strcmp(line->name, ".balign") == 0){
      if((uint64)value > 0xffffffffULL)
        return -1;
      align = value;
    } else {
      if(value > 20)
        return -1;
      align = 1U << value;
    }
    return align_current_section(object, align);
  }

  //directivas ignoradas, se aceptan pero todvaía no las aplico
  if(strcmp(line->name, ".option") == 0 ||
     strcmp(line->name, ".file") == 0 ||
     strcmp(line->name, ".ident") == 0 ||
     strcmp(line->name, ".attribute") == 0 ||
     strcmp(line->name, ".type") == 0 ||
     strcmp(line->name, ".size") == 0)
    return 0;

  return -1;
}

/*Va a convertir un símbolo interno del array temporal de símbolos en una entrada ELF real de la tabla .symtab*/
static int
append_final_symbol(struct Xv6TccObjectBuilder *object, int slot)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int shndx;
  int info;
  uint index;

  //se obtiene el símbolo
  symbol = &object->symbols[slot];
  shndx = symbol->defined ? symbol->section_index : XV6_TCC_SHN_UNDEF;
  //construye el campo st_info del símbolo que combina: el binding (local o global) y el tipo del símbolo (aunque de momento todos etndrán de tipo "notype")
  info = xv6_tcc_elf_st_info(symbol->binding, XV6_TCC_STT_NOTYPE);

  //inserta el símbolo en .strtab y añade la entrada ELF64_Sym a .symtab, y nos devuelve su índice
  if(xv6_tcc_put_elf_sym(object->symtab, object->strtab,
                         symbol->value, symbol->size,
                         info, 0, shndx,
                         symbol->name, &index) < 0)
    return -1;
  //el índice devuelto al haberlo intersetado en las tablas será usado para las relocaciones finales
  symbol->elf_index = index;
  return 0;
}

static int
initialize_object(struct Xv6TccObjectBuilder *object,
                  struct Xv6TccElfBuffer *text,
                  struct Xv6TccElfBuffer *rodata,
                  struct Xv6TccElfBuffer *data,
                  struct Xv6TccElfBuffer *symtab,
                  struct Xv6TccElfStringTable *strtab,
                  struct Xv6TccElfBuffer *rela_text,
                  struct Xv6TccElfBuffer *rela_rodata,
                  struct Xv6TccElfBuffer *rela_data)
{
  if(!object || !text || !rodata || !data || !symtab || !strtab ||
     !rela_text || !rela_rodata || !rela_data ||
     !text->data || !rodata->data || !data->data ||
     !symtab->data || !strtab->data || !rela_text->data ||
     !rela_rodata->data || !rela_data->data)
    return -1;

  object->text = text;
  object->rodata = rodata;
  object->data = data;
  object->symtab = symtab;
  object->strtab = strtab;
  object->rela_text = rela_text;
  object->rela_rodata = rela_rodata;
  object->rela_data = rela_data;
  object->current_section = XV6_TCC_SHN_TEXT;
  object->text_align = 4;
  object->rodata_align = 1;
  object->data_align = 1;
  object->bss_align = 1;

  text->size = 0;
  rodata->size = 0;
  data->size = 0;
  symtab->size = 0;
  strtab->size = 0;
  rela_text->size = 0;
  rela_rodata->size = 0;
  rela_data->size = 0;
  return 0;
}

int
xv6_tcc_object_init_full(struct Xv6TccObjectBuilder *object,
                         struct Xv6TccElfBuffer *text,
                         struct Xv6TccElfBuffer *rodata,
                         struct Xv6TccElfBuffer *data,
                         struct Xv6TccElfBuffer *symtab,
                         struct Xv6TccElfStringTable *strtab,
                         struct Xv6TccElfBuffer *rela_text,
                         struct Xv6TccElfBuffer *rela_rodata,
                         struct Xv6TccElfBuffer *rela_data)
{
  if(!object)
    return -1;
  memset(object, 0, sizeof(*object));
  return initialize_object(object, text, rodata, data, symtab, strtab,
                           rela_text, rela_rodata, rela_data);
}

int
xv6_tcc_object_init_sections(struct Xv6TccObjectBuilder *object,
                             struct Xv6TccElfBuffer *text,
                             struct Xv6TccElfBuffer *rodata,
                             struct Xv6TccElfBuffer *data,
                             struct Xv6TccElfBuffer *symtab,
                             struct Xv6TccElfStringTable *strtab,
                             struct Xv6TccElfBuffer *rela_text)
{
  if(!object)
    return -1;
  memset(object, 0, sizeof(*object));
  object->fallback_rela_rodata.data = object->fallback_rela_rodata_byte;
  object->fallback_rela_rodata.capacity =
      sizeof(object->fallback_rela_rodata_byte);
  object->fallback_rela_data.data = object->fallback_rela_data_byte;
  object->fallback_rela_data.capacity =
      sizeof(object->fallback_rela_data_byte);
  return initialize_object(object, text, rodata, data, symtab, strtab,
                           rela_text, &object->fallback_rela_rodata,
                           &object->fallback_rela_data);
}

int
xv6_tcc_object_init(struct Xv6TccObjectBuilder *object,
                    struct Xv6TccElfBuffer *text,
                    struct Xv6TccElfBuffer *symtab,
                    struct Xv6TccElfStringTable *strtab,
                    struct Xv6TccElfBuffer *rela_text)
{
  if(!object)
    return -1;
  //se inicializan el estado del fichero objeto en memoria a 0 todos sus campos 
  memset(object, 0, sizeof(*object));
  object->fallback_rodata.data = object->fallback_rodata_byte;
  object->fallback_rodata.capacity = sizeof(object->fallback_rodata_byte);
  object->fallback_data.data = object->fallback_data_byte;
  object->fallback_data.capacity = sizeof(object->fallback_data_byte);
  object->fallback_rela_rodata.data = object->fallback_rela_rodata_byte;
  object->fallback_rela_rodata.capacity =
      sizeof(object->fallback_rela_rodata_byte);
  object->fallback_rela_data.data = object->fallback_rela_data_byte;
  object->fallback_rela_data.capacity =
      sizeof(object->fallback_rela_data_byte);
  return initialize_object(object, text, &object->fallback_rodata,
                           &object->fallback_data, symtab, strtab,
                           rela_text, &object->fallback_rela_rodata,
                           &object->fallback_rela_data);
}

/*Procesa una línea ya analizada por el analizador de líneas xv6_tcc_parse_line()
para meterla al fichero objeto. También procesa directivas, líneas vacías y líneas con etiquetas, no solo líneas con instrucciones*/
int
xv6_tcc_object_process_line(struct Xv6TccObjectBuilder *object,
                            const struct Xv6TccParsedLine *line)
{
  //se valida que exista el objeto y la línea. No se pueden agregar más líneas si el fichero objeto se ha finalizdo
  if(!object || !line || object->finalized)
    return -1;

  //si la línea tiene una etiqueta, hay que definirla
  if(line->has_label && define_label(object, line->label) < 0)
    return -1;
  //si la línea está contiene solo una etiqueta "ej: loop:", pues la etiqueta fue procesada en el paso anterior así qeu no se hace nada
  if(line->kind == XV6_TCC_LINE_EMPTY)
    return 0;
  //si es una directiva la procesa
  if(line->kind == XV6_TCC_LINE_DIRECTIVE)
    return process_directive(object, line);
  //si la línea es una instrución, se procesa
  if(line->kind == XV6_TCC_LINE_INSTRUCTION)
    return emit_instruction(object, line);
  return -1;
}

int
xv6_tcc_object_finalize(struct Xv6TccObjectBuilder *object)
{
  struct Xv6TccAssemblerRelocation *relocation;
  struct Xv6TccAssemblerSymbol *symbol;
  uint empty_name_offset;
  uint null_symbol_index;
  uint64 info;
  int i;

  //no se puede finalizar un fichero objeto 2 veces
  if(!object || object->finalized)
    return -1;

  //se vacían las tablas finales del fichero objeto ya que se van a construir desde cero utilizando los arrays auxiliares
  //no vaciamos .text porque el código generado debe conservarse
  object->symtab->size = 0;
  object->strtab->size = 0;
  object->rela_text->size = 0;
  object->rela_rodata->size = 0;
  object->rela_data->size = 0;

  if(xv6_tcc_put_elf_str(object->strtab, "", &empty_name_offset) < 0 ||
     empty_name_offset != 0)
    return -1;
  //la entrada cero de .symtab también debe ser el símbolo nulo con todos sus campos a cero. Los símbolos sreales empiezan en el índice 1
  if(xv6_tcc_put_elf_sym_raw(object->symtab, 0, 0, 0,
                             0, 0, XV6_TCC_SHN_UNDEF,
                             &null_symbol_index) < 0 ||
     null_symbol_index != 0)
    return -1;

  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(!symbol->defined && symbol->binding == XV6_TCC_STB_LOCAL)
      symbol->binding = XV6_TCC_STB_GLOBAL;
  }
  /*Se agregan los símbolos locales a la tabla ".symtab".
  ELF exige colocar primero los símbolos locales*/
  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(symbol->binding == XV6_TCC_STB_LOCAL &&
       append_final_symbol(object, i) < 0)
      return -1;
  }

  object->first_global_symbol =
      object->symtab->size / sizeof(struct Xv6TccElfSym);

  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(symbol->binding != XV6_TCC_STB_LOCAL &&
       append_final_symbol(object, i) < 0)
      return -1;
  }

  /*En este punto tendría en .symtab los siguiente por ejemplo:
  índice 0 → nulo
  índice 1 → done, local
  índice 2 → main, global
  índice 3 → external_func, global indefinido*/
  //recorro el array de relocaciones pendientes para meter en el buffer de reloaciones todas las relocaciones acumuladas en el array auxiliar
  for(i = 0; i < object->relocation_count; i++){
    struct Xv6TccElfBuffer *rela_section;

    //saco la relocación
    relocation = &object->relocations[i];
    //obtengo el símbolo asociado a esa relocación
    symbol = &object->symbols[relocation->symbol_slot];
    //creo el campo r_info de esa relocación
    info = xv6_tcc_elf_r_info(symbol->elf_index, relocation->type);
    if(relocation->section_index == XV6_TCC_SHN_TEXT)
      rela_section = object->rela_text;
    else if(relocation->section_index == XV6_TCC_SHN_RODATA)
      rela_section = object->rela_rodata;
    else if(relocation->section_index == XV6_TCC_SHN_DATA)
      rela_section = object->rela_data;
    else
      return -1;
    if(xv6_tcc_put_elf_rela(rela_section,
                            relocation->offset,
                            info,
                            relocation->addend) < 0)
      return -1;
  }

  //marco el fichero objeto como finalizado
  object->finalized = 1;
  return 0;
}

/*Función auxiliar para buscar un símbolo dado un nombre*/
const struct Xv6TccAssemblerSymbol *
xv6_tcc_object_find_symbol(const struct Xv6TccObjectBuilder *object,
                           const char *name)
{
  int slot;

  slot = symbol_slot_by_name(object, name);
  if(slot < 0)
    return 0;
  return &object->symbols[slot];
}

/*Función auxiliar para buscar un símbolo dado su posición o índice en el array de símbolos*/
const struct Xv6TccAssemblerRelocation *
xv6_tcc_object_relocation_at(const struct Xv6TccObjectBuilder *object,
                             int index)
{
  if(!object || index < 0 || index >= object->relocation_count)
    return 0;
  return &object->relocations[index];
}
