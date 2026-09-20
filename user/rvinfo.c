#include "kernel/types.h"
#include "user/user.h"

#include "user/tinycc/xv6_tcc_line.h"
#include "user/tinycc/xv6_tcc_insn.h"

/*
Programa didactico que reutiliza el parser y el codificador
de asxv6 para mostrar cómo se analiza y codifica una línea
*/

enum EncodingFormat {
  FORMAT_UNKNOWN,
  FORMAT_R,
  FORMAT_I,
  FORMAT_S,
  FORMAT_B,
  FORMAT_U,
  FORMAT_J,
  FORMAT_FIXED
};

/*
Une argv[1], argv[2], ... para reconstruir la línea de
ensamblador introducida en la shell
*/
static int
join_arguments(int argc, char **argv, char *output, int capacity)
{
  int i;
  int j;
  int position;

  position = 0;

  for(i = 1; i < argc; i++){
    if(i > 1){
      if(position + 1 >= capacity)
        return -1;

      output[position++] = ' ';
    }

    for(j = 0; argv[i][j] != 0; j++){
      if(position + 1 >= capacity)
        return -1;

      output[position++] = argv[i][j];
    }
  }

  output[position] = 0;
  return 0;
}


/*
Devuelve un nombre legible para el tipo de línea
obtenido por el parser
*/
static const char *
line_kind_name(int kind)
{
  switch(kind){
  case XV6_TCC_LINE_EMPTY:
    return "EMPTY";

  case XV6_TCC_LINE_INSTRUCTION:
    return "INSTRUCTION";

  case XV6_TCC_LINE_DIRECTIVE:
    return "DIRECTIVE";
  }

  return "UNKNOWN";
}


/*
Devuelve un nombre legible para la familia interna
de la instrucción
*/
static const char *
instruction_kind_name(int kind)
{
  switch(kind){
  case XV6_TCC_INSN_R:
    return "R";

  case XV6_TCC_INSN_I:
    return "I";

  case XV6_TCC_INSN_LOAD:
    return "LOAD";

  case XV6_TCC_INSN_STORE:
    return "STORE";

  case XV6_TCC_INSN_BRANCH:
    return "BRANCH";

  case XV6_TCC_INSN_U:
    return "U";

  case XV6_TCC_INSN_JAL:
    return "JAL";

  case XV6_TCC_INSN_JALR:
    return "JALR";

  case XV6_TCC_INSN_SHIFTI64:
    return "SHIFTI64";

  case XV6_TCC_INSN_SHIFTIW:
    return "SHIFTIW";

  case XV6_TCC_INSN_FIXED:
    return "FIXED";

  case XV6_TCC_PSEUDO_NOP:
    return "PSEUDO_NOP";

  case XV6_TCC_PSEUDO_MV:
    return "PSEUDO_MV";

  case XV6_TCC_PSEUDO_NOT:
    return "PSEUDO_NOT";

  case XV6_TCC_PSEUDO_NEG:
    return "PSEUDO_NEG";

  case XV6_TCC_PSEUDO_RET:
    return "PSEUDO_RET";

  case XV6_TCC_PSEUDO_JR:
    return "PSEUDO_JR";

  case XV6_TCC_PSEUDO_J:
    return "PSEUDO_J";

  case XV6_TCC_PSEUDO_LI:
    return "PSEUDO_LI";

  case XV6_TCC_PSEUDO_CALL:
    return "PSEUDO_CALL";

  case XV6_TCC_PSEUDO_LA:
    return "PSEUDO_LA";
  }

  return "UNKNOWN";
}


/*
Imprime exactamente digits digitos hexadecimales

Se hace manualmente porque el printf reducido de xv6
no implementa necesariamente formatos como %08x
*/
static void
print_hex(uint value, int digits)
{
  static const char hex[] = "0123456789abcdef";
  char buffer[8];
  int i;

  if(digits > 8)
    digits = 8;

  for(i = digits - 1; i >= 0; i--){
    buffer[i] = hex[value & 0xf];
    value >>= 4;
  }

  write(1, buffer, digits);
}


/*
Imprime un campo binario con una anchura fija
*/
static void
print_bits(uint value, int width)
{
  char buffer[32];
  int i;

  if(width > 32)
    width = 32;

  for(i = 0; i < width; i++){
    int bit;

    bit = width - 1 - i;
    buffer[i] = ((value >> bit) & 1) ? '1' : '0';
  }

  write(1, buffer, width);
}


/*
Determina el formato fisico de la palabra ya codificada de forma que una pseudoinstrucción de una sola palabra
se visualice segun la instrucción real que genera

Por ejemplo

ret
  -> jalr x0, 0(ra)
  -> formato I
*/
static int
encoding_format(const struct Xv6TccInstruction *instruction,
                uint word)
{
  uint opcode;

  if(instruction->kind == XV6_TCC_INSN_FIXED)
    return FORMAT_FIXED;

  opcode = word & 0x7f;

  switch(opcode){
  case 0x33:
  case 0x3b:
    return FORMAT_R;

  case 0x13:
  case 0x1b:
  case 0x03:
  case 0x67:
    return FORMAT_I;

  case 0x23:
    return FORMAT_S;

  case 0x63:
    return FORMAT_B;

  case 0x37:
  case 0x17:
    return FORMAT_U;

  case 0x6f:
    return FORMAT_J;
  }

  return FORMAT_UNKNOWN;
}


/*
Muestra los campos fisicos que forman la instrucción
codificada en 32 bits
*/
static void
print_encoding(uint word,
               const struct Xv6TccInstruction *instruction)
{
  int format;

  format = encoding_format(instruction, word);

  printf("\nCODIFICACION\n");
  printf("------------\n");

  printf("word = 0x");
  print_hex(word, 8);
  printf("\n");

  printf("bytes little-endian = ");

  print_hex(word & 0xff, 2);
  printf(" ");

  print_hex((word >> 8) & 0xff, 2);
  printf(" ");

  print_hex((word >> 16) & 0xff, 2);
  printf(" ");

  print_hex((word >> 24) & 0xff, 2);
  printf("\n\n");


  if(format == FORMAT_R){
    uint funct7;
    uint rs2;
    uint rs1;
    uint funct3;
    uint rd;
    uint opcode;

    funct7 = (word >> 25) & 0x7f;
    rs2 = (word >> 20) & 0x1f;
    rs1 = (word >> 15) & 0x1f;
    funct3 = (word >> 12) & 0x7;
    rd = (word >> 7) & 0x1f;
    opcode = word & 0x7f;

    printf("Formato R\n");

    printf("funct7 [31:25] = 0x%x\n", funct7);
    printf("rs2    [24:20] = %d (0x%x)\n", rs2, rs2);
    printf("rs1    [19:15] = %d (0x%x)\n", rs1, rs1);
    printf("funct3 [14:12] = 0x%x\n", funct3);
    printf("rd     [11:7]  = %d (0x%x)\n", rd, rd);
    printf("opcode [6:0]   = 0x%x\n", opcode);

    printf("\nbits:\n");

    print_bits(funct7, 7);
    printf(" | ");
    print_bits(rs2, 5);
    printf(" | ");
    print_bits(rs1, 5);
    printf(" | ");
    print_bits(funct3, 3);
    printf(" | ");
    print_bits(rd, 5);
    printf(" | ");
    print_bits(opcode, 7);
    printf("\n");

    return;
  }


  if(format == FORMAT_I){
    uint immediate;
    uint rs1;
    uint funct3;
    uint rd;
    uint opcode;

    immediate = (word >> 20) & 0xfff;
    rs1 = (word >> 15) & 0x1f;
    funct3 = (word >> 12) & 0x7;
    rd = (word >> 7) & 0x1f;
    opcode = word & 0x7f;

    printf("Formato I\n");

    printf("imm    [31:20] = 0x");
    print_hex(immediate, 3);
    printf("\n");

    printf("rs1    [19:15] = %d (0x%x)\n", rs1, rs1);
    printf("funct3 [14:12] = 0x%x\n", funct3);
    printf("rd     [11:7]  = %d (0x%x)\n", rd, rd);
    printf("opcode [6:0]   = 0x%x\n", opcode);

    printf("\nbits:\n");

    print_bits(immediate, 12);
    printf(" | ");
    print_bits(rs1, 5);
    printf(" | ");
    print_bits(funct3, 3);
    printf(" | ");
    print_bits(rd, 5);
    printf(" | ");
    print_bits(opcode, 7);
    printf("\n");

    return;
  }


  if(format == FORMAT_S){
    uint imm_high;
    uint rs2;
    uint rs1;
    uint funct3;
    uint imm_low;
    uint opcode;

    imm_high = (word >> 25) & 0x7f;
    rs2 = (word >> 20) & 0x1f;
    rs1 = (word >> 15) & 0x1f;
    funct3 = (word >> 12) & 0x7;
    imm_low = (word >> 7) & 0x1f;
    opcode = word & 0x7f;

    printf("Formato S\n");

    printf("imm[11:5] [31:25] = 0x%x\n", imm_high);
    printf("rs2       [24:20] = %d\n", rs2);
    printf("rs1       [19:15] = %d\n", rs1);
    printf("funct3    [14:12] = 0x%x\n", funct3);
    printf("imm[4:0]  [11:7]  = 0x%x\n", imm_low);
    printf("opcode    [6:0]   = 0x%x\n", opcode);

    return;
  }


  if(format == FORMAT_B){
    printf("Formato B\n");

    printf("imm[12]   [31]    = %d\n",
           (word >> 31) & 1);

    printf("imm[10:5] [30:25] = 0x%x\n",
           (word >> 25) & 0x3f);

    printf("rs2       [24:20] = %d\n",
           (word >> 20) & 0x1f);

    printf("rs1       [19:15] = %d\n",
           (word >> 15) & 0x1f);

    printf("funct3    [14:12] = 0x%x\n",
           (word >> 12) & 0x7);

    printf("imm[4:1]  [11:8]  = 0x%x\n",
           (word >> 8) & 0xf);

    printf("imm[11]   [7]     = %d\n",
           (word >> 7) & 1);

    printf("opcode    [6:0]   = 0x%x\n",
           word & 0x7f);

    return;
  }


  if(format == FORMAT_U){
    printf("Formato U\n");

    printf("imm[31:12] = 0x%x\n",
           (word >> 12) & 0xfffff);

    printf("rd[11:7]   = %d\n",
           (word >> 7) & 0x1f);

    printf("opcode[6:0] = 0x%x\n",
           word & 0x7f);

    return;
  }


  if(format == FORMAT_J){
    printf("Formato J\n");

    printf("imm[20]    [31]    = %d\n",
           (word >> 31) & 1);

    printf("imm[10:1]  [30:21] = 0x%x\n",
           (word >> 21) & 0x3ff);

    printf("imm[11]    [20]    = %d\n",
           (word >> 20) & 1);

    printf("imm[19:12] [19:12] = 0x%x\n",
           (word >> 12) & 0xff);

    printf("rd         [11:7]  = %d\n",
           (word >> 7) & 0x1f);

    printf("opcode     [6:0]   = 0x%x\n",
           word & 0x7f);

    return;
  }


  if(format == FORMAT_FIXED){
    printf("Instruccion fija\n");
    printf("La palabra completa esta almacenada en la tabla\n");
    return;
  }

  printf("Formato de codificacion no identificado\n");
}


int
main(int argc, char **argv)
{
  char source[XV6_TCC_LINE_TEXT_MAX];
  struct Xv6TccParsedLine line;
  const struct Xv6TccInstruction *instruction;
  uint word;
  int i;

  if(argc < 2){
    fprintf(2,
      "uso: rvinfo <linea ensamblador>\n");

    fprintf(2,
      "ejemplo: rvinfo loop: addi t4, t4, 1\n");

    exit(1);
  }

  if(join_arguments(argc, argv,
                    source, sizeof(source)) < 0){
    fprintf(2, "rvinfo: linea demasiado larga\n");
    exit(1);
  }

  printf("LINEA\n");
  printf("-----\n");
  printf("%s\n\n", source);


  // Se reutiliza directamente el parser de lineas de asxv6
  if(xv6_tcc_parse_line(source, &line) < 0){
    fprintf(2, "rvinfo: linea no valida\n");
    exit(1);
  }


  printf("ANALISIS DEL PARSER\n");
  printf("-------------------\n");

  printf("has_label = %d\n", line.has_label);

  if(line.has_label){
    printf("label = \"%s\"\n", line.label);

    /*
    defined no forma parte de Xv6TccParsedLine, este valor indica unicamente de forma visual que la línea
    contiene la definición de una etiqueta
    */
    printf("defined_here = 1\n");
  }

  printf("kind = %s\n", line_kind_name(line.kind));

  if(line.kind == XV6_TCC_LINE_EMPTY)
    exit(0);

  printf("name = \"%s\"\n", line.name);
  printf("operand_count = %d\n", line.operand_count);

  for(i = 0; i < line.operand_count; i++)
    printf("operands[%d] = \"%s\"\n",
           i, line.operands[i]);


  /*
    Las directivas son analizadas por el parser pero no tienen
    una codificación como instrucción RV64I, ssu efecto se procesa posteriormente durante la construcción
    del objeto ELF
    */
  if(line.kind == XV6_TCC_LINE_DIRECTIVE){
    printf("\nDIRECTIVA\n");
    printf("---------\n");

    printf("nombre = \"%s\"\n", line.name);
    printf("operandos = %d\n", line.operand_count);

    for(i = 0; i < line.operand_count; i++)
        printf("operands[%d] = \"%s\"\n",
            i, line.operands[i]);

    printf("\nNo genera una codificacion de instruccion RV64I\n");

    exit(0);
  }

  /*
  Recupero la misma entrada de la tabla de instrucciones de ensamblador asxv6
  */
  instruction = xv6_tcc_find_instruction(line.name);

  if(!instruction){
    fprintf(2, "rvinfo: instruccion desconocida\n");
    exit(1);
  }


  printf("\nDESCRIPTOR DE INSTRUCCION\n");
  printf("-------------------------\n");

  printf("instruction_kind = %s\n",
         instruction_kind_name(instruction->kind));

  if(instruction->kind == XV6_TCC_INSN_FIXED){
    printf("palabra fija = 0x");
    print_hex(instruction->opcode, 8);
    printf("\n");
  } else {
    printf("opcode = 0x%x\n", instruction->opcode);
    printf("funct3 = 0x%x\n", instruction->funct3);
    printf("funct7 = 0x%x\n", instruction->funct7);
  }

  printf("operand_count = %d\n",
         instruction->operand_count);


  /*
  Reutilizo el codificador del ensamblador asxv6
  */
  if(xv6_tcc_encode_parsed_instruction(&line, &word) < 0){
    printf("\nCODIFICACION\n");
    printf("------------\n");

    printf("No puede obtenerse una unica palabra definitiva\n");
    printf("con el codificador aislado\n");

    printf("La instruccion puede requerir una relocacion,\n");
    printf("un simbolo o una expansion de varias palabras\n");

    exit(0);
  }


  print_encoding(word, instruction);

  exit(0);
}