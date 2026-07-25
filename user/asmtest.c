#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_asm.h"
#include "user/tinycc/xv6_tcc_elf.h"

static void
fail(const char *test)
{
  fprintf(2, "asmtest: fallo: %s\n", test);
  exit(1);
}

static void
expect_word(const char *test, uint actual, uint expected)
{
  if(actual != expected){
    fprintf(2, "asmtest: %s: obtenido=0x%x esperado=0x%x\n",
            test, actual, expected);
    exit(1);
  }
}

int
main(void)
{
  uchar text_data[64];
  struct Xv6TccElfBuffer text;
  uint add_word;
  uint addi_word;
  uint sd_word;
  uint beq_word;
  uint lui_word;
  uint jal_word;
  uint offset;

  memset(text_data, 0xaa, sizeof(text_data));
  text.data = text_data;
  text.size = 0;
  text.capacity = sizeof(text_data);

  if(xv6_tcc_encode_r(0x33, 0, 0, 5, 6, 7, &add_word) < 0)
    fail("codificacion R de add");
  expect_word("add t0, t1, t2", add_word, 0x007302b3U);

  if(xv6_tcc_encode_i(0x13, 0, 10, 0, 42, &addi_word) < 0)
    fail("codificacion I de addi");
  expect_word("addi a0, zero, 42", addi_word, 0x02a00513U);

  if(xv6_tcc_encode_s(0x23, 3, 2, 11, 16, &sd_word) < 0)
    fail("codificacion S de sd");
  expect_word("sd a1, 16(sp)", sd_word, 0x00b13823U);

  if(xv6_tcc_encode_b(0x63, 0, 10, 11, 8, &beq_word) < 0)
    fail("codificacion B de beq");
  expect_word("beq a0, a1, 8", beq_word, 0x00b50463U);

  if(xv6_tcc_encode_u(0x37, 10, 0x12345, &lui_word) < 0)
    fail("codificacion U de lui");
  expect_word("lui a0, 0x12345", lui_word, 0x12345537U);

  if(xv6_tcc_encode_j(0x6f, 1, 8, &jal_word) < 0)
    fail("codificacion J de jal");
  expect_word("jal ra, 8", jal_word, 0x008000efU);

  if(xv6_tcc_encode_i(0x13, 0, 10, 0, 4096, &offset) == 0)
    fail("rechazo de inmediato I fuera de rango");

  if(xv6_tcc_encode_b(0x63, 0, 10, 11, 3, &offset) == 0)
    fail("rechazo de desplazamiento B impar");

  if(xv6_tcc_encode_j(0x6f, 1, 3, &offset) == 0)
    fail("rechazo de desplazamiento J impar");

  if(xv6_tcc_encode_r(0x33, 0, 0, 32, 6, 7, &offset) == 0)
    fail("rechazo de registro inexistente");

  if(xv6_tcc_emit32(&text, add_word, &offset) < 0 || offset != 0)
    fail("emision de add");
  if(xv6_tcc_emit32(&text, addi_word, &offset) < 0 || offset != 4)
    fail("emision de addi");
  if(xv6_tcc_emit32(&text, sd_word, &offset) < 0 || offset != 8)
    fail("emision de sd");
  if(xv6_tcc_emit32(&text, beq_word, &offset) < 0 || offset != 12)
    fail("emision de beq");
  if(xv6_tcc_emit32(&text, lui_word, &offset) < 0 || offset != 16)
    fail("emision de lui");
  if(xv6_tcc_emit32(&text, jal_word, &offset) < 0 || offset != 20)
    fail("emision de jal");

  if(text.size != 24)
    fail("tamano final de .text");

  if(text.data[0] != 0xb3 || text.data[1] != 0x02 ||
     text.data[2] != 0x73 || text.data[3] != 0x00)
    fail("orden little-endian de add");

  if(text.data[4] != 0x13 || text.data[5] != 0x05 ||
     text.data[6] != 0xa0 || text.data[7] != 0x02)
    fail("orden little-endian de addi");

  fprintf(1, "codificacion RV64I minima verificada\n");
  fprintf(1, "  R add:  0x%x\n", add_word);
  fprintf(1, "  I addi: 0x%x\n", addi_word);
  fprintf(1, "  S sd:   0x%x\n", sd_word);
  fprintf(1, "  B beq:  0x%x\n", beq_word);
  fprintf(1, "  U lui:  0x%x\n", lui_word);
  fprintf(1, "  J jal:  0x%x\n", jal_word);
  fprintf(1, "  .text:  %d bytes\n", text.size);
  fprintf(1, " prueba xv6 superada\n");
  exit(0);
}
