#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_asm.h"
#include "user/tinycc/xv6_tcc_elf.h"

static void
fail(const char *test)
{
  fprintf(2, "operandtest: fallo: %s\n", test);
  exit(1);
}

static void
expect_register(const char *text, int expected)
{
  int actual;

  if(xv6_tcc_parse_register(text, &actual) < 0 || actual != expected){
    fprintf(2, "operandtest: registro %s incorrecto\n", text);
    exit(1);
  }
}

static void
expect_integer(const char *text, long expected)
{
  long actual;

  if(xv6_tcc_parse_integer(text, &actual) < 0 || actual != expected){
    fprintf(2, "operandtest: inmediato %s incorrecto\n", text);
    exit(1);
  }
}

static void
expect_memory(const char *text, long expected_offset, int expected_base)
{
  long actual_offset;
  int actual_base;

  if(xv6_tcc_parse_memory_operand(text, &actual_offset, &actual_base) < 0 ||
     actual_offset != expected_offset || actual_base != expected_base){
    fprintf(2, "operandtest: memoria %s incorrecta\n", text);
    exit(1);
  }
}

static void
expect_word(const char *test, uint actual, uint expected)
{
  if(actual != expected){
    fprintf(2, "operandtest: %s: obtenido=0x%x esperado=0x%x\n",
            test, actual, expected);
    exit(1);
  }
}

int
main(void)
{
  uchar text_data[32];
  struct Xv6TccElfBuffer text;
  int rd;
  int rs1;
  int rs2;
  long immediate;
  long memory_offset;
  uint addi_word;
  uint sd_word;
  uint ld_word;
  uint offset;

  expect_register("zero", 0);
  expect_register("ra", 1);
  expect_register("sp", 2);
  expect_register("fp", 8);
  expect_register("s0", 8);
  expect_register("a0", 10);
  expect_register("a7", 17);
  expect_register("t6", 31);
  expect_register("x0", 0);
  expect_register("x31", 31);
  expect_register("  a1  ", 11);

  if(xv6_tcc_parse_register("x32", &rd) == 0)
    fail("rechazo de x32");
  if(xv6_tcc_parse_register("a8", &rd) == 0)
    fail("rechazo de a8");

  expect_integer("42", 42);
  expect_integer("-16", -16);
  expect_integer("0x123", 0x123);
  expect_integer("-0x20", -0x20);
  expect_integer("0b101010", 42);
  expect_integer("1_024", 1024);

  if(xv6_tcc_parse_integer("0xg1", &immediate) == 0)
    fail("rechazo de hexadecimal invalido");
  if(xv6_tcc_parse_integer("", &immediate) == 0)
    fail("rechazo de inmediato vacio");

  expect_memory("16(sp)", 16, 2);
  expect_memory("-8(s0)", -8, 8);
  expect_memory("(a0)", 0, 10);
  expect_memory(" 0x20(x5) ", 0x20, 5);

  if(xv6_tcc_parse_memory_operand("16sp)", &memory_offset, &rs1) == 0)
    fail("rechazo de memoria sin parentesis izquierdo");
  if(xv6_tcc_parse_memory_operand("16(x32)", &memory_offset, &rs1) == 0)
    fail("rechazo de base inexistente");
  if(xv6_tcc_parse_memory_operand("16(sp)extra", &memory_offset, &rs1) == 0)
    fail("rechazo de texto tras memoria");

  if(xv6_tcc_parse_register("a0", &rd) < 0 ||
     xv6_tcc_parse_register("zero", &rs1) < 0 ||
     xv6_tcc_parse_integer("42", &immediate) < 0)
    fail("operandos de addi");
  if(xv6_tcc_encode_i(0x13, 0, rd, rs1, immediate, &addi_word) < 0)
    fail("codificacion textual de addi");
  expect_word("addi a0, zero, 42", addi_word, 0x02a00513U);

  if(xv6_tcc_parse_register("a1", &rs2) < 0 ||
     xv6_tcc_parse_memory_operand("16(sp)", &memory_offset, &rs1) < 0)
    fail("operandos de sd");
  if(xv6_tcc_encode_s(0x23, 3, rs1, rs2,
                      memory_offset, &sd_word) < 0)
    fail("codificacion textual de sd");
  expect_word("sd a1, 16(sp)", sd_word, 0x00b13823U);

  if(xv6_tcc_parse_register("a0", &rd) < 0 ||
     xv6_tcc_parse_memory_operand("-8(sp)", &memory_offset, &rs1) < 0)
    fail("operandos de ld");
  if(xv6_tcc_encode_i(0x03, 3, rd, rs1,
                      memory_offset, &ld_word) < 0)
    fail("codificacion textual de ld");
  expect_word("ld a0, -8(sp)", ld_word, 0xff813503U);

  memset(text_data, 0xaa, sizeof(text_data));
  text.data = text_data;
  text.size = 0;
  text.capacity = sizeof(text_data);

  if(xv6_tcc_emit32(&text, addi_word, &offset) < 0 || offset != 0)
    fail("emision de addi textual");
  if(xv6_tcc_emit32(&text, sd_word, &offset) < 0 || offset != 4)
    fail("emision de sd textual");
  if(xv6_tcc_emit32(&text, ld_word, &offset) < 0 || offset != 8)
    fail("emision de ld textual");

  if(text.size != 12)
    fail("tamano final de .text");

  fprintf(1, "[etapa 4] operandos RV64I verificados\n");
  fprintf(1, "  a0 -> x%d\n", rd);
  fprintf(1, "  16(sp) -> offset=16 base=x2\n");
  fprintf(1, "  addi: 0x%x\n", addi_word);
  fprintf(1, "  sd:   0x%x\n", sd_word);
  fprintf(1, "  ld:   0x%x\n", ld_word);
  fprintf(1, "  .text: %d bytes\n", text.size);
  fprintf(1, "Etapa 4: prueba xv6 superada\n");
  exit(0);
}
