/*
xv6_tcc_line.h

Analizador educativo de líneas de ensamblador.
Separa una línea en etiqueta opcional, nombre y operandos. Las etiquetas y
las directivas todavía no modifican tablas de símbolos ni secciones ELF.
*/
#ifndef XV6_TCC_LINE_H
#define XV6_TCC_LINE_H

#include "user/tinycc/xv6_tcc_insn.h"

#define XV6_TCC_LINE_TEXT_MAX 256 /*longitud máxima de una línea de código (255 + nulo) 
EJ: loop: addi a0, a0, -1 # decrementar contador 
normalmente la línea no será tan larga pero se pone un tamaño considerable para no utilizar memoria dinámica*/
#define XV6_TCC_LINE_NAME_MAX 64 //longitud máxima de un nombre, ya sean etiquetas (loop, while), instrucciones (add, addi) y directivas (aquí van las secciones y otras directivas como 
//.globl main, .word 42, .asciz "Hola"), las directivas siempre comienzan por "." posteriormente he agregado que los símbolos también tiene cómo longiutd máxima esa constante
#define XV6_TCC_LINE_OPERAND_MAX 128 //longitud máxima de texto de un operando
#define XV6_TCC_LINE_MAX_OPERANDS 3 //número máximo de operandos en una instrucción. De momento solo permito 3 pero luego se permitirán más para cosas como .byte 1, 2, 3, 4

//cada línea del código se clasificará en estos 3 tipos 
enum Xv6TccLineKind {
  XV6_TCC_LINE_EMPTY, /*Aquí se incluyen los comentarios "#comentario", líneas vacías y líneas con solo una etiqueta (Ej: "loop:")*/
  XV6_TCC_LINE_INSTRUCTION, //es una línea con una instrucción ej add a0, a1, a2
  XV6_TCC_LINE_DIRECTIVE //es un directiva (las directivas comiennzan con "." EJ: .text)
};

//aquí se guarda el resultado de analizar una línea
struct Xv6TccParsedLine {
  int has_label; //la línea incluye una etiqueta o no Ej: loop: addi a0, a0, -1, la línea tiene una etiqueta
  char label[XV6_TCC_LINE_NAME_MAX]; //guarda el nombre de la etiqueta, si no tiene lo pongo vacío
  int kind; //se guarda el tipo de línea según el enum Xv6TccLineKind (línea vacía, instrucción o driectiva)
  char name[XV6_TCC_LINE_NAME_MAX]; //guarda el nombre principal de la línea: ej: instrucción addi a0, a1, 42 --> guarda "addi". en un directiva guardaría ".asciz"
  int operand_count; //contador del número de operandos de la línea
  char operands[XV6_TCC_LINE_MAX_OPERANDS][XV6_TCC_LINE_OPERAND_MAX]; //es un array que guarda todos los operandos (como el límite de moemnto es 3 permite hasta 3 operandos)
};

//comprueba si una cadena puede utilizarse como identificardor de una etiqueta o símbolo
int xv6_tcc_valid_identifier(const char *text);

/*Recibe la línea original y devuelve la línea ya clasificada/analizada en el struct Xv6TccParsedLine*/
int xv6_tcc_parse_line(const char *text,
                       struct Xv6TccParsedLine *line);


//codifica la instrucción ya clasificada en xv6_tcc_parse_line
int xv6_tcc_encode_parsed_instruction(
    const struct Xv6TccParsedLine *line,
    uint *word);

/*codifica la instrucción ya clasificada en xv6_tcc_parse_line y además escribe los 4 bytes (32 bits) de la instrucción binaria 
en el buffer que representa la sección .text, todavía no en el fichero ELF definitivo en disco*/
int xv6_tcc_emit_parsed_instruction(
    const struct Xv6TccParsedLine *line,
    struct Xv6TccElfBuffer *text_section,
    uint *offset);

#endif
