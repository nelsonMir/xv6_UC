/*
xv6_tcc_insn.h

Tabla reducida de instrucciones RV64I y expansión de pseudoinstrucciones.
La tabla relaciona cada nombre textual con el formato y los campos binarios
que consumen los codificadores de xv6_tcc_asm.c.
*/
#ifndef XV6_TCC_INSN_H
#define XV6_TCC_INSN_H

#include "user/tinycc/xv6_tcc_asm.h"

/*Creo este enumerado para tener nombres descriptivos y así 
distinguir los tipos de instrucción.
EL valor entero de este enumerado se guardará en el struct de abajo Xv6TccInstruction
en el campo "kind" para indicar el tipo de instrucción */
enum Xv6TccInstructionKind {
  XV6_TCC_INSN_R,
  XV6_TCC_INSN_I,
  XV6_TCC_INSN_LOAD, //OJO utiliza el codificador I pero su sintaxis es diferente, por lo que sus operandos aquí se analizarán diferente 
  XV6_TCC_INSN_STORE,
  XV6_TCC_INSN_BRANCH,
  XV6_TCC_INSN_U,
  XV6_TCC_INSN_JAL,
  XV6_TCC_INSN_JALR, //OJO utiliza el codificador I pero su sintaxis es diferente, por lo que sus operandos aquí se analizarán diferente 
  XV6_TCC_PSEUDO_NOP,
  XV6_TCC_PSEUDO_MV,
  XV6_TCC_PSEUDO_NOT,
  XV6_TCC_PSEUDO_NEG,
  XV6_TCC_PSEUDO_RET,
  XV6_TCC_PSEUDO_JR,
  XV6_TCC_PSEUDO_J,
  XV6_TCC_PSEUDO_LI
};

/*Este struct se utiliza para describir una instrucción utilizando sus diferentes campos*/
struct Xv6TccInstruction {
  const char *name; //nombre de la operacción EJ: "add"
  int kind; //aquí guardaré cómo debe interpretarse esa instrucción: EJemplo un add y un sub son de la misma familia, y pueden ser del tipo dos registros y un inmediato
  uint opcode; //el código de la familia de la operación EJ: add --> "0x33"
  uint funct3; //selecciona una operación dentro de esa familia (ejemplo add y sub pertenecen a la misma opcode familia)
  uint funct7; //permiten distinguir operaciones que aún coinciden en el formato T
  int operand_count; //número de operandos que espera la isntrucción
};

//busca una instrucción por su nombre y devuelve el código hexadecimal de la operación (en realidad devuelve un puntero 
//al array instructions referenciando a la entrada correspondiente a esa operación, esa tabla/arrya contendrá la info necesaria sobre la operación en este 
//orden: nombre, tipo, opcode, funct3, funct7, cantidad de operandos )
const struct Xv6TccInstruction *
xv6_tcc_find_instruction(const char *name);

//recibe la instrucción y se va a encargar (por medio de las funciones auxiliares) generar la codificación de la instrucción en 32 bits
int xv6_tcc_encode_named_instruction(const char *name,
                                      const char *operand1,
                                      const char *operand2,
                                      const char *operand3,
                                      uint *word);

#endif
