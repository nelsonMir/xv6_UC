#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_line.h"

static void
fail(const char *test)
{
  fprintf(2, "linetest: fallo: %s\n", test);
  exit(1);
}

static void
expect_parse_failure(const char *text)
{
  struct Xv6TccParsedLine line;

  if(xv6_tcc_parse_line(text, &line) == 0){
    fprintf(2, "linetest: se esperaba error: %s\n", text);
    exit(1);
  }
}

static uint
parse_and_encode(const char *text)
{
  struct Xv6TccParsedLine line;
  uint word;

  if(xv6_tcc_parse_line(text, &line) < 0 ||
     xv6_tcc_encode_parsed_instruction(&line, &word) < 0){
    fprintf(2, "linetest: no se pudo codificar: %s\n", text);
    exit(1);
  }

  return word;
}

int
main(void)
{
  struct Xv6TccParsedLine line;
  struct Xv6TccElfBuffer text;
  uchar text_data[32];
  uint offset;
  uint word;

  if(xv6_tcc_parse_line("   # comentario", &line) < 0 ||
     line.kind != XV6_TCC_LINE_EMPTY || line.has_label)
    fail("linea de comentario");

  if(xv6_tcc_parse_line("main:", &line) < 0 ||
     !line.has_label || strcmp(line.label, "main") != 0 ||
     line.kind != XV6_TCC_LINE_EMPTY)
    fail("etiqueta aislada");

  if(xv6_tcc_parse_line("loop: add a0, a1, a2 # suma", &line) < 0 ||
     !line.has_label || strcmp(line.label, "loop") != 0 ||
     line.kind != XV6_TCC_LINE_INSTRUCTION ||
     strcmp(line.name, "add") != 0 || line.operand_count != 3 ||
     strcmp(line.operands[0], "a0") != 0 ||
     strcmp(line.operands[1], "a1") != 0 ||
     strcmp(line.operands[2], "a2") != 0)
    fail("etiqueta e instruccion");

  word = parse_and_encode("  addi a0, sp, 16  # reserva");
  if(word != 0x01010513U)
    fail("codificacion de linea addi");

  word = parse_and_encode("sd a1, 16(sp)");
  if(word != 0x00b13823U)
    fail("codificacion de linea sd");

  if(xv6_tcc_parse_line("beq a0, a1, destino", &line) < 0 ||
     line.kind != XV6_TCC_LINE_INSTRUCTION ||
     strcmp(line.operands[2], "destino") != 0)
    fail("branch simbolico reconocido");
  if(xv6_tcc_encode_parsed_instruction(&line, &word) == 0)
    fail("branch simbolico no debe codificarse aun");

  if(xv6_tcc_parse_line(".text", &line) < 0 ||
     line.kind != XV6_TCC_LINE_DIRECTIVE || line.operand_count != 0)
    fail("directiva text");

  if(xv6_tcc_parse_line(".globl main", &line) < 0 ||
     line.kind != XV6_TCC_LINE_DIRECTIVE ||
     line.operand_count != 1 || strcmp(line.operands[0], "main") != 0)
    fail("directiva globl");

  if(xv6_tcc_parse_line(".word 1, 2, 3", &line) < 0 ||
     line.kind != XV6_TCC_LINE_DIRECTIVE ||
     line.operand_count != 3 || strcmp(line.operands[2], "3") != 0)
    fail("directiva word");

  if(xv6_tcc_parse_line("msg: .asciz \"Hola, # mundo\" # comentario", &line) < 0 ||
     !line.has_label || strcmp(line.label, "msg") != 0 ||
     line.kind != XV6_TCC_LINE_DIRECTIVE || line.operand_count != 1 ||
     strcmp(line.operands[0], "\"Hola, # mundo\"") != 0)
    fail("cadena con coma y almohadilla");

  expect_parse_failure("add a0,,a1");
  expect_parse_failure("1mala: ret");
  expect_parse_failure("inexistente a0");
  expect_parse_failure(".desconocida 1");
  expect_parse_failure("ld a0, 16(sp");

  memset(text_data, 0, sizeof(text_data));
  text.data = text_data;
  text.size = 0;
  text.capacity = sizeof(text_data);

  if(xv6_tcc_parse_line("add a0, a1, a2", &line) < 0 ||
     xv6_tcc_emit_parsed_instruction(&line, &text, &offset) < 0 ||
     offset != 0)
    fail("emit add");

  if(xv6_tcc_parse_line("sd a1, 16(sp)", &line) < 0 ||
     xv6_tcc_emit_parsed_instruction(&line, &text, &offset) < 0 ||
     offset != 4)
    fail("emit sd");

  if(xv6_tcc_parse_line("ret", &line) < 0 ||
     xv6_tcc_emit_parsed_instruction(&line, &text, &offset) < 0 ||
     offset != 8 || text.size != 12)
    fail("emit ret");

  if(text.data[0] != 0x33 || text.data[1] != 0x85 ||
     text.data[2] != 0xc5 || text.data[3] != 0x00 ||
     text.data[8] != 0x67 || text.data[9] != 0x80 ||
     text.data[10] != 0x00 || text.data[11] != 0x00)
    fail("bytes emitidos");

  fprintf(1, "analizador de lineas verificado\n");
  fprintf(1, "  etiqueta: loop\n");
  fprintf(1, "  instruccion: addi a0,sp,16 -> 0x%x\n", 0x01010513U);
  fprintf(1, "  memoria: sd a1,16(sp) -> 0x%x\n", 0x00b13823U);
  fprintf(1, "  directivas: .text, .globl, .word, .asciz\n");
  fprintf(1, "  .text emitido: %d bytes\n", text.size);
  fprintf(1, "prueba xv6 superada\n");
  exit(0);
}
