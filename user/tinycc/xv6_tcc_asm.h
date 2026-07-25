/*
  xv6_tcc_asm.h
 
  Codificación mínima de instrucciones RV64I inspirada en el modelo de
  emisores por formato de TinyCC riscv64-asm.c.
 
  Las fucnciones ya pueden procesar ellas misma el número de registro e inmediatos
  para ello se añaden nombres de registros, enteros y operandos de memoria.
Todavía no se busca el nombre de una instrucción en una tabla ni se procesan
etiquetas o expresiones simbólicas
 */
#ifndef XV6_TCC_ASM_H
#define XV6_TCC_ASM_H

#include "user/tinycc/xv6_tcc_elf.h"

int xv6_tcc_parse_register(const char *text, int *reg);
int xv6_tcc_parse_integer(const char *text, long *value);
int xv6_tcc_parse_memory_operand(const char *text,
                                 long *offset, int *base_register);

int xv6_tcc_encode_r(uint opcode, uint funct3, uint funct7,
                     int rd, int rs1, int rs2, uint *word);
int xv6_tcc_encode_i(uint opcode, uint funct3,
                     int rd, int rs1, long imm, uint *word);
int xv6_tcc_encode_s(uint opcode, uint funct3,
                     int rs1, int rs2, long imm, uint *word);
int xv6_tcc_encode_b(uint opcode, uint funct3,
                     int rs1, int rs2, long imm, uint *word);
int xv6_tcc_encode_u(uint opcode, int rd, uint imm20, uint *word);
int xv6_tcc_encode_j(uint opcode, int rd, long imm, uint *word);
int xv6_tcc_emit32(struct Xv6TccElfBuffer *section,
                   uint word, uint *offset);

#endif
