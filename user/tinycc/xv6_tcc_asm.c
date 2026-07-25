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

//comprueba que sea un registro válido
static int
valid_register(int reg)
{
  return reg >= 0 && reg <= 31;
}

//verifica que el operando sea correcto
static int
valid_opcode(uint opcode)
{
  return opcode <= 0x7fU; //devuelve el resultado de una comparación  (0 o 1) ejj: 0x13 <= 0x7fU
  /*Como el opcode tiene 7 bits los valores mínimos y máximos son:
  0000000 -> 0
  1111111 -> 127 y 127 es 0x7f */
}

static int
valid_funct3(uint funct3)
{
  return funct3 <= 0x7U; //devuelve el resultado de una comparación (0 o 1)
  /*funct3 tienen 3 bits entonces el valor máximo es 111 -> 0x7*/
}

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

/*Llamadas para codificar una instrucción según su tipo
LOs formatos son: R, I, S, B, U y J

cada llamada devuelve la instrcción codificada en "*word", ósea 
converte una instrucción RISV en una palabra binaria. EJ: 
addi a0, zero, 42 ---> 0x02a00513

*/

//fortmato R: instrcciones con registros
int
xv6_tcc_encode_r(uint opcode, uint funct3, uint funct7,
                 int rd, int rs1, int rs2, uint *word)
{
  if(!word || !valid_opcode(opcode) || !valid_funct3(funct3) ||
     !valid_funct7(funct7) || !valid_register(rd) ||
     !valid_register(rs1) || !valid_register(rs2))
    return -1;

  *word = opcode |
          ((uint)rd << 7) |
          (funct3 << 12) |
          ((uint)rs1 << 15) |
          ((uint)rs2 << 20) |
          (funct7 << 25);

  /*Esta última operación hace un OR bit a bit, es decir, combina todos los bits para formar la palabra, ej: 
  inmediato: 0x02a00000
  rd:        0x00000500
  opcode:    0x00000013
             ----------
  resultado: 0x02a00513*/
  return 0;
}

//fortmato I: instrcciones con inmediato
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

//fortmato S: stores
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

//fortmato B: branches
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

//fortmato J: instrcciones con inmediato con bits bits 31:12
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
