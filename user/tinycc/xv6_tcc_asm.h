/*
xv6_tcc_asm.h

Codificación de los formatos necesarios para las 52 instrucciones RV64I y
análisis de operandos textuales inspirado en TinyCC riscv64-asm.c.

Los nombres de instrucciones se mantienen en xv6_tcc_insn.c
 este módulo
valida registros, inmediatos, shifts y operandos de memoria y produce las
palabras binarias de 32 bits.
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
int xv6_tcc_encode_shift_i(uint opcode, uint funct3, uint funct_top,
                           int shamt_bits, int rd, int rs1,
                           long shamt, uint *word);
int xv6_tcc_encode_s(uint opcode, uint funct3,
                     int rs1, int rs2, long imm, uint *word);
int xv6_tcc_encode_b(uint opcode, uint funct3,
                     int rs1, int rs2, long imm, uint *word);
int xv6_tcc_encode_u(uint opcode, int rd, uint imm20, uint *word);
int xv6_tcc_encode_j(uint opcode, int rd, long imm, uint *word);
int xv6_tcc_emit32(struct Xv6TccElfBuffer *section,
                   uint word, uint *offset);

#endif
