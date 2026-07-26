/*
  xv6_tcc_asm.c
 
  Primera extracción del ensamblador RISC-V de TinyCC para xv6: extrae los bytes 
  de las instrucciones para la codificación
  En palabras simples:
  converte una instrucción RISV en una palabra binaria. EJ: addi a0, zero, 42 ---> 0x02a00513
  Se escriben en formato little endian y son de 32 bits
 
  basado en:
    TinyCC riscv64-asm.c
    https://raw.githubusercontent.com/TinyCC/tinycc/d9d02c56401e43be43760b63f7d82f771a7ed1f6/riscv64-asm.c
 
  TinyCC separa la emisión según los formatos R, I, S, B, U y J y termina
  escribiendo una palabra de 32 bits en cur_text_section. En esta etapa se
  conservan esas dos ideas, pero se eliminan TCCState, Operand, tokens,
  símbolos y relocaciones. La escritura se realiza en Xv6TccElfBuffer.
 */

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_asm.h"

//longitud maxima de texto de un operando, un operando nunca sera tan grande pero por error un usuario podría esccribir algo muy grande 
//y se debería poder procesar el error
#define XV6_TCC_OPERAND_TEXT_MAX 128

/*SIrve para asociar un nombre textual de un registro con su valor en dígito respectivamente
Ósea, los registros se pueden escribir númericamente x0, x1... x31 pero también se pueden usar 
los alias ABI:
zero -> x0
ra   -> x1
sp   -> x2
t0   -> x5
fp   -> x8
s0   -> x8
a0   -> x10
a7   -> x17
t6   -> x31
antes solo podíamos procesos los registros si ya venían como dítigos. AHora se pueden interpretar 
tanto en formato x0...x31 como formato ABI */
struct Xv6TccRegisterName {
  const char *name;
  int number;
};

/*Tabla de nombres de registros
Cada nombre ABI se corresponde con un número real del registro.
LE he puesto estático porque solo se puede ver en este archivo y es una constante 
para no modificarla durante la ejecución. Hay registros que tiene dos nombres ABI:
fp y s0 son el registro físico 8*/
static const struct Xv6TccRegisterName register_names[] = {
  { "zero", 0 }, { "ra", 1 }, { "sp", 2 }, { "gp", 3 },
  { "tp", 4 }, { "t0", 5 }, { "t1", 6 }, { "t2", 7 },
  { "s0", 8 }, { "fp", 8 }, { "s1", 9 }, { "a0", 10 },
  { "a1", 11 }, { "a2", 12 }, { "a3", 13 }, { "a4", 14 },
  { "a5", 15 }, { "a6", 16 }, { "a7", 17 }, { "s2", 18 },
  { "s3", 19 }, { "s4", 20 }, { "s5", 21 }, { "s6", 22 },
  { "s7", 23 }, { "s8", 24 }, { "s9", 25 }, { "s10", 26 },
  { "s11", 27 }, { "t3", 28 }, { "t4", 29 }, { "t5", 30 },
  { "t6", 31 }
};

//comprueba que sea un registro válido
static int
valid_register(int reg)
{
  return reg >= 0 && reg <= 31;
}

//verifica que la operación sea correcta
//el código de la familia de la operación EJ: add --> "0x33"
//al ser de 7 bits permite 2 a la 7 = 128 operaciones y RISCV necesita muchas más operaciones, por eso las operaciones se agrupan en familias
static int
valid_opcode(uint opcode)
{
  return opcode <= 0x7fU; //devuelve el resultado de una comparación  (0 o 1) ejj: 0x13 <= 0x7fU
  /*Como el opcode tiene 7 bits los valores mínimos y máximos son:
  0000000 -> 0
  1111111 -> 127 y 127 es 0x7f */
}

//selecciona una operación dentro de esa familia (ejemplo add y sub pertenecen a la misma opcode familia)
static int
valid_funct3(uint funct3)
{
  return funct3 <= 0x7U; //devuelve el resultado de una comparación (0 o 1)
  /*funct3 tienen 3 bits entonces el valor máximo es 111 -> 0x7*/
}

//permiten distinguir operaciones que aún coinciden en el formato R
static int
valid_funct7(uint funct7)
{
  return funct7 <= 0x7fU;
  /*funct7 tiene 7 bits entonces el valor máximo es 1111111 (127) --> en hexa 0x7f*/
}

//comprueba si un número cabe en una determinada cantidad de bits con signo
static int
fits_signed(long value, int bits)
{
  long minimum = -(1L << (bits - 1));
  long maximum = (1L << (bits - 1)) - 1;

  return value >= minimum && value <= maximum;
}

/*----------------------------------------------------------
Ya se permite recibir operandos escritos como texto
*/

/*Esta función quita espacios de un registro ej: "   a0  " --> "a0"
devuelve la cadena sin espacios en "Output"*/
static int
copy_trimmed(const char *text, char *output, int output_size)
{
  const char *begin; //apunta al primer caracter útil 
  const char *end; //apuntará al siguiente caracteres después del último carácter útil
  int length; //cantidad de caracteres a copiar

  if(!text || !output || output_size <= 0)
    return -1;

  //se saltan espacios últiles mientras que el caracter actual sea un espacio o el tabulador
  //al terminar el bucle, begin apunta al primer caracter.
  //ósea se elimina los espacios iniciales: "   a0  " al final -> "a0  "
  begin = text;
  while(*begin == ' ' || *begin == '\t')
    begin++;

  //sacamos la longitud de la cadena Ej:
  //"a0  " begin apunta a "a", pero hay 3 caracteres más, el 0 y 2 espaciones, entonces la longitud 
  //total el 4, así que end=4
  end = begin + strlen(begin);

  //se elimnan los espacios finales "a0  " -> "a0", para ello hoy se va retrocediendo desde el final hasta ponerse 
  //en el caracter juto después de 0 del a0
  while(end > begin && (end[-1] == ' ' || end[-1] == '\t'))
    end--;

  //Calcula la longitud
  length = end - begin;
  if(length <= 0 || length >= output_size)
    return -1;

  memmove(output, begin, length); //copia la cadena en output
  output[length] = 0; //agrega el caracter nulo al final de la cadena 
  return 0;
}

/*Es una función auxiliar utilizada por xv6_tcc_parse_register y xv6_tcc_parse_memory_operand.
COnvierte un registro textual (aquí ya se recibe el registro en formato de número pero al final es un string así que se debe convertir) en un número long: 
EJ: esto acepta 
"42" --> 42
"-16"  --> -16
"0x20" -->32
"-0x20" --> -32
"0b1010" --> 10
"1_024" --> 1024 en este caso se ignorar las "_"
DEvuleve el resultado en "result"

COmo dije lo usaré como auxiliar, porque aquí ya llega el registro solo como número pero en un string, las otras 2 funciones se encargarán de limpiar 
para dejar solo los números pero en un string (quitan los prefijos o simplemente transforman EJ: sp --> 2)*/
int
xv6_tcc_parse_integer(const char *text, long *result)
{
  char buffer[XV6_TCC_OPERAND_TEXT_MAX]; //array local para almacenar la copia del texto al quitar espacios
  const char *cursor;
  unsigned long value;
  unsigned long maximum;
  unsigned long limit;
  int negative;
  int base;
  int digit_count;

  //quita espacios
  //compruebo que el puntero result no sea nulo y que el quitar espacios funcione
  if(!result || copy_trimmed(text, buffer, sizeof(buffer)) < 0)
    return -1;

  //cursor para recorrer la cadena
  cursor = buffer;

  //procesa el signo, inicialmente supone que es positivo, por eso negative = 0;
  negative = 0;
  if(*cursor == '+' || *cursor == '-'){
    negative = *cursor == '-';
    cursor++;
  }

  //detecta la base numérica, se asume inicialmente que es base 10
  base = 10;
  //si inicia con 0x es base hexa, entonces omite ese prefijo y solo deja los numeros
  if(cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')){
    base = 16;
    cursor += 2;
    //si es base 2 ej 0b1010
  } else if(cursor[0] == '0' &&
            (cursor[1] == 'b' || cursor[1] == 'B')){
    base = 2;
    cursor += 2;
  }


  //variables para la conversión
  value = 0; 
  digit_count = 0; //cuenta numero de digitos leídos omitiendo signo
  maximum = ~0UL >> 1; //se pone el máximo unsigned long desplazado un espacio a la derecha: 0UL es cero unsiged long 00000...000 y el ~ lo inverte todo a 1's y luego el >> 1 lo desplaza un espacio a la derecha 
  //entonces 01111...11 --> máximo long positvo
  limit = negative ? maximum + 1UL : maximum;

  //bucle hasta llegar al caracter nulo del final de la cadena 
  while(*cursor){
    int digit;

    //se ignoran los "_" entonces si el texto es 1_024 pasaría a -> 1024
    if(*cursor == '_'){
      cursor++;
      continue;
    }


    //convierte un caracter en un dígito
    if(*cursor >= '0' && *cursor <= '9')
      digit = *cursor - '0'; //'7' - '0' = 7
    //también con los hexadecimales tanto para mayuscula y minuscula
    else if(*cursor >= 'a' && *cursor <= 'f')
      digit = *cursor - 'a' + 10; //A–F
    else if(*cursor >= 'A' && *cursor <= 'F')
      digit = *cursor - 'A' + 10;
    else
      return -1;

    //comoprueba que el digito es valido en la base actual (ya que un digito puede ser valido en una base pero no en otra)
    if(digit >= base)
      return -1;
    if(value > (limit - (unsigned long)digit) / (unsigned long)base)
      return -1;

    //comprobacion desbordamiento
    value = value * (unsigned long)base + (unsigned long)digit;
    //aumenta el contador de dígitos
    digit_count++;
    //aumenta el cursor al sig digito
    cursor++;
  }

  //conprueba que habia al menos 1 digito
  if(digit_count == 0)
    return -1;

  //aplica signo
  if(negative){
    if(value == maximum + 1UL)
      *result = -((long)maximum) - 1L;
    else
      *result = -(long)value;
  } else {
    *result = (long)value;
  }

  return 0;
}

/*Convierte el nombre de un registro en un número entre 0 y 31 
permite aceptar 2 tipos de valores 
nombres numéricos: x0, x1, ..., x31
nombres ABI: zero, ra, sp, a0, s1, t6..*/
int
xv6_tcc_parse_register(const char *text, int *reg)
{
  char name[XV6_TCC_OPERAND_TEXT_MAX];
  int i;

  //quita espacios
  if(!reg || copy_trimmed(text, name, sizeof(name)) < 0)
    return -1;

    //comprueba que el registro con nombre numerico es valido (formato x0...x31)
    //para ello comprueba que los digitos despues de la "x" sean digitos validos
  if(name[0] == 'x' && name[1] != 0){
    long number;

    for(i = 1; name[i] != 0; i++)
      if(name[i] < '0' || name[i] > '9')
        break;

    if(name[i] == 0 && xv6_tcc_parse_integer(name + 1, &number) == 0 &&
       number >= 0 && number <= 31){
      *reg = (int)number;
      return 0;
    }
  }

  //si el nombre del registro está en formato ABI (sp, a1...)
  for(i = 0;
      i < (int)(sizeof(register_names) / sizeof(register_names[0]));
      i++){
    if(strcmp(name, register_names[i].name) == 0){
      *reg = register_names[i].number;
      return 0;
    }
  }

  return -1;
}

/*INterpreta operando con el formato "desplazamiento(registro)" Ejemplos:
16(sp)
-8(s0)
0(a0)
(sp)

devuelve 2 valores: offset, el registro base
EJ: 16(sp)
offset = 16
base_register = 2*/
int
xv6_tcc_parse_memory_operand(const char *text,
                              long *offset, int *base_register)
{
  char buffer[XV6_TCC_OPERAND_TEXT_MAX];
  char offset_text[XV6_TCC_OPERAND_TEXT_MAX];
  char register_text[XV6_TCC_OPERAND_TEXT_MAX];
  char *left; //puntero parentesis (
  char *right;//puntero parentesis )
  int offset_length;
  int register_length;

  if(!offset || !base_register ||
     copy_trimmed(text, buffer, sizeof(buffer)) < 0)
    return -1;

  left = strchr(buffer, '(');
  if(!left)
    return -1;

  right = strchr(left + 1, ')');
  if(!right || right[1] != 0 || strchr(left + 1, '(') ||
     strchr(right + 1, ')'))
    return -1;

  offset_length = left - buffer;
  register_length = right - left - 1;
  if(offset_length >= (int)sizeof(offset_text) ||
     register_length <= 0 ||
     register_length >= (int)sizeof(register_text))
    return -1;

  if(offset_length == 0){
    offset_text[0] = '0';
    offset_text[1] = 0;
  } else {
    memmove(offset_text, buffer, offset_length);
    offset_text[offset_length] = 0;
  }

  memmove(register_text, left + 1, register_length);
  register_text[register_length] = 0;

  if(xv6_tcc_parse_integer(offset_text, offset) < 0)
    return -1;
  if(xv6_tcc_parse_register(register_text, base_register) < 0)
    return -1;

  return 0;
}


/*--------------------------------------------------------
Llamadas para codificar una instrucción según su tipo
LOs formatos son: R, I, S, B, U y J

cada llamada devuelve la instrcción codificada en "*word", ósea 
converte una instrucción RISV en una palabra binaria. EJ: 
addi a0, zero, 42 ---> 0x02a00513

*/

//fortmato R: instrucciones con registros -> tres registros rd, rs1, rs2
/*recibe los campos de la instrucción ya dividido en varias variables y devuelve 
la instrucción codificada en binario en "word"*/
int
xv6_tcc_encode_r(uint opcode, uint funct3, uint funct7,
                 int rd, int rs1, int rs2, uint *word)
{
  //valido que cada uno de los campos de la instrucción sean válidos
  if(!word || !valid_opcode(opcode) || !valid_funct3(funct3) ||
     !valid_funct7(funct7) || !valid_register(rd) ||
     !valid_register(rs1) || !valid_register(rs2))
    return -1;

  *word = opcode |
          ((uint)rd << 7) | //el rd << 7 desplaza los bits en la var rd 7 posiciones a la izq
                            //ej: antes 00000000 00000000 00000000 00001010
                            //despues   00000000 00000000 00000101 00000000
                            //esto se hace así porque en esta instrucción el campo rd ocupa los bits 11:7 (del 11 al 7)
                            //el bit situado más a la derecha es el bit 0, el menos significativo, debo hacer un esquema en la memoria de la instrucción 
          (funct3 << 12) |
          ((uint)rs1 << 15) |
          ((uint)rs2 << 20) |
          (funct7 << 25);

  /*Esta última operación hace un OR bit a bit, es decir, combina todos los bits para formar la palabra utilizando 
  los distintios argumentos ej: 
  inmediato: 0x02a00000
  rd:        0x00000500
  opcode:    0x00000013
             ----------
  resultado: 0x02a00513*/
  return 0;
}

//fortmato I: instrcciones con inmediato -> dos registro y un inmediato rd, rs1, imm
int
xv6_tcc_encode_i(uint opcode, uint funct3,
                 int rd, int rs1, long imm, uint *word)
{
  if(!word || !valid_opcode(opcode) || !valid_funct3(funct3) ||
     !valid_register(rd) || !valid_register(rs1) ||
     !fits_signed(imm, 12))
    return -1;

  *word = opcode |
          ((uint)rd << 7) |
          (funct3 << 12) |
          ((uint)rs1 << 15) |
          (((uint)imm & 0xfffU) << 20);
  return 0;
}

//fortmato S: stores, registro fuente y memoria -> rs2, offset(rs1)
int
xv6_tcc_encode_s(uint opcode, uint funct3,
                 int rs1, int rs2, long imm, uint *word)
{
  uint encoded;

  if(!word || !valid_opcode(opcode) || !valid_funct3(funct3) ||
     !valid_register(rs1) || !valid_register(rs2) ||
     !fits_signed(imm, 12))
    return -1;

  encoded = (uint)imm & 0xfffU;
  *word = opcode |
          ((encoded & 0x1fU) << 7) |
          (funct3 << 12) |
          ((uint)rs1 << 15) |
          ((uint)rs2 << 20) |
          (((encoded >> 5) & 0x7fU) << 25);
  return 0;
}

//fortmato B: branches, dos registros y desplazamiento: rs1, rs2, imm
int
xv6_tcc_encode_b(uint opcode, uint funct3,
                 int rs1, int rs2, long imm, uint *word)
{
  uint encoded;

  if(!word || !valid_opcode(opcode) || !valid_funct3(funct3) ||
     !valid_register(rs1) || !valid_register(rs2) ||
     !fits_signed(imm, 13) || (imm & 1L) != 0)
    return -1;

  encoded = (uint)imm & 0x1fffU;
  *word = opcode |
          (((encoded >> 11) & 0x1U) << 7) |
          (((encoded >> 1) & 0xfU) << 8) |
          (funct3 << 12) |
          ((uint)rs1 << 15) |
          ((uint)rs2 << 20) |
          (((encoded >> 5) & 0x3fU) << 25) |
          (((encoded >> 12) & 0x1U) << 31);
  return 0;
}

//fortmato U: instrcciones con inmediato con bits bits 31:12 -> rd, imm20
int
xv6_tcc_encode_u(uint opcode, int rd, uint imm20, uint *word)
{
  if(!word || !valid_opcode(opcode) || !valid_register(rd) ||
     imm20 > 0xfffffU)
    return -1;

  *word = opcode |
          ((uint)rd << 7) |
          (imm20 << 12);
  return 0;
}

//fortmato J: instrcciones 
int
xv6_tcc_encode_j(uint opcode, int rd, long imm, uint *word)
{
  uint encoded;

  if(!word || !valid_opcode(opcode) || !valid_register(rd) ||
     !fits_signed(imm, 21) || (imm & 1L) != 0)
    return -1;

  encoded = (uint)imm & 0x1fffffU;
  *word = opcode |
          ((uint)rd << 7) |
          (((encoded >> 12) & 0xffU) << 12) |
          (((encoded >> 11) & 0x1U) << 20) |
          (((encoded >> 1) & 0x3ffU) << 21) |
          (((encoded >> 20) & 0x1U) << 31);
  return 0;
}

/*Las instrucciones están en la sección .text pero de momento las funciones para codificar 
no meten la instrucción condificada en su sección respectiva, para ello se debe llamar a esta función actual 
con la palabra codificada. 
Una vez la mete en la sección devuelve su offset.
Se separan los 4 bytes (son palabras de 32 bits)  y se escriben little endian*/
int
xv6_tcc_emit32(struct Xv6TccElfBuffer *section,
               uint word, uint *offset)
{
  uint position;

  if(!section || !offset)
    return -1;

  if(xv6_tcc_section_add(section, 4, 4, &position) < 0)
    return -1;

  section->data[position + 0] = word & 0xffU;
  section->data[position + 1] = (word >> 8) & 0xffU;
  section->data[position + 2] = (word >> 16) & 0xffU;
  section->data[position + 3] = (word >> 24) & 0xffU;
  *offset = position;
  return 0;
}
