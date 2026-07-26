#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_insn.h"

static void
fail(const char *test)
{
  fprintf(2, "instructiontest: fallo: %s\n", test);
  exit(1);
}

static void
expect_instruction(const char *name,
                   const char *operand1,
                   const char *operand2,
                   const char *operand3,
                   uint expected)
{
  uint actual;

  if(xv6_tcc_encode_named_instruction(name,
                                       operand1,
                                       operand2,
                                       operand3,
                                       &actual) < 0){
    fprintf(2, "instructiontest: no se pudo codificar %s\n", name);
    exit(1);
  }

  if(actual != expected){
    fprintf(2, "instructiontest: %s: obtenido=0x%x esperado=0x%x\n",
            name, actual, expected);
    exit(1);
  }
}

int
main(void)
{
  const struct Xv6TccInstruction *instruction;
  uint word;

  instruction = xv6_tcc_find_instruction("add");
  if(!instruction || instruction->kind != XV6_TCC_INSN_R ||
     instruction->opcode != 0x33 || instruction->funct3 != 0 ||
     instruction->funct7 != 0 || instruction->operand_count != 3)
    fail("descriptor de add");

  instruction = xv6_tcc_find_instruction("sub");
  if(!instruction || instruction->funct7 != 0x20)
    fail("descriptor de sub");

  instruction = xv6_tcc_find_instruction("ret");
  if(!instruction || instruction->kind != XV6_TCC_PSEUDO_RET ||
     instruction->operand_count != 0)
    fail("descriptor de ret");

  expect_instruction("add", "t0", "t1", "t2", 0x007302b3U);
  expect_instruction("sub", "t0", "t1", "t2", 0x407302b3U);
  expect_instruction("and", "a0", "a1", "a2", 0x00c5f533U);
  expect_instruction("addi", "a0", "zero", "42", 0x02a00513U);
  expect_instruction("ld", "a0", "-8(sp)", 0, 0xff813503U);
  expect_instruction("sd", "a1", "16(sp)", 0, 0x00b13823U);
  expect_instruction("beq", "a0", "a1", "8", 0x00b50463U);
  expect_instruction("lui", "a0", "0x12345", 0, 0x12345537U);
  expect_instruction("jal", "ra", "8", 0, 0x008000efU);
  expect_instruction("jalr", "ra", "0(sp)", 0, 0x000100e7U);

  expect_instruction("nop", 0, 0, 0, 0x00000013U);
  expect_instruction("mv", "a0", "a1", 0, 0x00058513U);
  expect_instruction("not", "a0", "a1", 0, 0xfff5c513U);
  expect_instruction("neg", "a0", "a1", 0, 0x40b00533U);
  expect_instruction("ret", 0, 0, 0, 0x00008067U);
  expect_instruction("jr", "a0", 0, 0, 0x00050067U);
  expect_instruction("j", "8", 0, 0, 0x0080006fU);
  expect_instruction("li", "a0", "42", 0, 0x02a00513U);

  if(xv6_tcc_find_instruction("inexistente") != 0)
    fail("rechazo de nombre inexistente");
  if(xv6_tcc_encode_named_instruction("add", "a0", "a1", 0, &word) == 0)
    fail("rechazo de operandos insuficientes");
  if(xv6_tcc_encode_named_instruction("ret", "a0", 0, 0, &word) == 0)
    fail("rechazo de operando extra");
  if(xv6_tcc_encode_named_instruction("li", "a0", "4096", 0, &word) == 0)
    fail("li grande aun no soportado");

  fprintf(1, "tabla RV64I y pseudoinstrucciones verificadas\n");
  fprintf(1, "  add t0,t1,t2: 0x%x\n", 0x007302b3U);
  fprintf(1, "  sub t0,t1,t2: 0x%x\n", 0x407302b3U);
  fprintf(1, "  mv a0,a1:     0x%x\n", 0x00058513U);
  fprintf(1, "  ret:          0x%x\n", 0x00008067U);
  fprintf(1, "  li a0,42:     0x%x\n", 0x02a00513U);
  fprintf(1, "prueba xv6 superada\n");
  exit(0);
}
