/*Ensamblador de RISC-V minimo que convierte un fichero en ensamblador en un fichero
objeto que es un ELF64 RISC-V relocatable, es decir, si hay una instruccion incompleta, 
osea, de la que no se sepa su direccion final, cuando el linker conozca la direccion final
de ese simbolo va a parchear esos bits. Osea las direcciones finales no se saben hasta enlazar*/
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAX_SRC     32768
#define MAX_TEXT    16384 //codigo fuente max longitud 16 KiB (zona .text)
#define MAX_RODATA  8192
#define MAX_DATA    8192
#define MAX_SYM     256
#define MAX_REL     256
#define MAX_STRTAB  8192

//posibles valores del puntero que indica la seccion actual que se esta procesando 
//Ej: si el ensamblador lee ".text" entonces el cursor "cursec = SEC_TEXT"
#define SEC_UNDEF   0
#define SEC_TEXT    1 //seccion .text
#define SEC_RODATA  2 //seccion .rodata
#define SEC_DATA    3 //seccion .data

#define EI_NIDENT   16

#define ET_REL      1
#define EM_RISCV    243
#define EV_CURRENT  1

#define SHT_NULL    0
#define SHT_PROGBITS 1
#define SHT_SYMTAB  2
#define SHT_STRTAB  3
#define SHT_RELA    4

#define SHF_WRITE      1
#define SHF_ALLOC      2
#define SHF_EXECINSTR  4

#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STT_NOTYPE  0

//tipos de relocaciones soportadas 
#define R_RISCV_NONE      0
#define R_RISCV_JAL       17
#define R_RISCV_HI20      26
#define R_RISCV_LO12_I    27

#define ELF64_R_INFO(sym, type) ((((uint64)(sym)) << 32) | ((type) & 0xffffffff))

typedef long int64;

struct Elf64_Ehdr {
  uchar  e_ident[EI_NIDENT];
  ushort e_type;
  ushort e_machine;
  uint   e_version;
  uint64 e_entry;
  uint64 e_phoff;
  uint64 e_shoff;
  uint   e_flags;
  ushort e_ehsize;
  ushort e_phentsize;
  ushort e_phnum;
  ushort e_shentsize;
  ushort e_shnum;
  ushort e_shstrndx;
};

struct Elf64_Shdr {
  uint   sh_name;
  uint   sh_type;
  uint64 sh_flags;
  uint64 sh_addr;
  uint64 sh_offset;
  uint64 sh_size;
  uint   sh_link;
  uint   sh_info;
  uint64 sh_addralign;
  uint64 sh_entsize;
};

struct Elf64_Sym {
  uint   st_name;
  uchar  st_info;
  uchar  st_other;
  ushort st_shndx;
  uint64 st_value;
  uint64 st_size;
};

struct Elf64_Rela {
  uint64 r_offset;
  uint64 r_info;
  int64  r_addend;
};


//tabla de simbolos para representar/almacenar etiquetas
//Ej: 
struct Sym {
  char name[32];
  int sec; //Valor del cursor para saber en que seccion esta definia (SEC_TEXT, SEC_RODATA, SEC_DATA)
  uint64 value; //offset de la etiqueta dentro de su seccion
  int defined; //Flag que indica si el simbolo esta definido, es decir "nombre:", el nombre con los ":" porque el simbolo 
  //puede aparecer en el codigo antes, y su definicion mas tarde, asi que cuando se encuentre la definicion pone "defined=1"
  int global; //indica si la etiqueta/simbolo debe aparecer como global en el ELF (para que sea referenciable por otros), 
  //por ejemplo el punto de entrada del kernel es global ".globl _start"

  //campos para la tabla ELF de simbolos (.symtab): Esta tabla no se conoce hasta escribir el ELF.
  //ELF no guarda nombres directamente dentro de cada simbolo, sino que guarda offsets de cada uno en esa tabla de simbolos/strings

  int outidx; //indice final del simbolo dentro de la tabla 
  uint stroff; //offset en la tabla de strings hacia ese simbolo, se guarda en .strtab

  /*EJ: .strtab:
  offset 0:  '\0'
  offset 1:  "_start\0"
  offset 8:  "msg\0"
  
  _start.stroff = 1
  msg.stroff    = 8*/
};


//estructura para la tabla de relocaciones para que el linker al saber la direccion final de las etiquetas pueda reemplazar 
//el valor correcto de la direccion luego aqui reescribiendo los bits 
struct Rel {
  int sec; //seccion donde esta la etiqueta a parchear
  uint off; //offset de la etiqueta dentro de la seccion
  uint type; //Tipo de relocacion ELF RISC V soportada por este ensamblador, son valores constantes que se pueden consultar arriba 
  char sym[32]; //nombre de la etiqueta
  int addend; //valor extra que se le puede sumar a la etiqueta, por ejemplo "msg + 4", osea la direccion le sumamos ese valor, pero de momento este 
  //valor es siempre 0, no soportamos expresiones asi "la a1, msg+4"
};

//buffer globales
//uso arrays estaticos para evitar usar malloc

static char src[MAX_SRC]; //guarda el fichero .s en texto completo

static uchar text[MAX_TEXT]; //guarda los bytes de la seccion .text (las instrucciones)
static uchar rodata[MAX_RODATA]; //guarda los bytes de la seccion .rodata
static uchar data[MAX_DATA]; //guarda los bytes de la seccion .data


//contadores para saber cuantos bytes se ocupan realmente en cada seccion, ya que 
//el tamanio esta capado para cada seccion
static int textsz;
static int rodatasz;
static int datasz;

static struct Sym syms[MAX_SYM];
static int nsyms;

static struct Rel rels[MAX_REL];
static int nrels;

static int cursec = SEC_TEXT;

static char strtab[MAX_STRTAB];
static int strsz;

static char shstrtab[MAX_STRTAB];
static int shstrsz;

/*FUnciones auxiliares*/

//sirve para abortar con error la generacion del fichero objeto. Recibe como argumento el error ocurrido 
//ej: die("unknown instruction");
//De momento, en ensamblaje puede fallar si una seccion se excede de tamanio, una directiva desconocida, un registro invalido, error escribiendo fichero, demasiados simbolos
static void
die(char *s)
{
  fprintf(2, "asxv6: %s\n", s);
  exit(1);
}

//compara si 2 strings son iguales, para hacer streq(op, "li") en vez de streq(op, "li")
static int
streq(char *a, char *b)
{
  return strcmp(a, b) == 0;
}

//permite detectar espaciones, tabuladores y saltos de linea. Se usa para limpiar lineas
static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//sirve para saltarse espacios e ir avanzado el puntero que lee el codigo
static char *
skip_spaces(char *p)
{
  while (*p && is_space(*p))
    p++;
  return p;
}

//elimina los espacions que estan puestos al final de una linea de codigo
//EJ: strcmp(op, "li") == 0 --------------> "li a0, 1"
static void
trim_right(char *s)
{
  int n = strlen(s);
  while (n > 0 && is_space(s[n - 1])) {
    s[n - 1] = 0;
    n--;
  }
}

//eliminar los comentarios. Una vez detecta la "#" elimina del texto todo lo que esta a la derecha de la "#"
static void
remove_comment(char *s)
{
  while (*s) {
    if (*s == '#') {
      *s = 0;
      return;
    }
    s++;
  }
}

//COpia los nombres de simbolos/eitquetas. limitado a 31 caracteres
static void
copy_name(char *dst, char *src)
{
  int i = 0;
  while (src[i] && i < 31) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}


//CONvierte un texto a un numero. Permite 3 tipos de codificaciones (bases):
//numeros naturales base 10, numeros enteros, y hexadecimal (0x)
static int
parse_num(char *s)
{
  int neg = 0;
  int base = 10;
  int x = 0;

  if (*s == '-') {
    neg = 1;
    s++;
  }

  if (s[0] == '0' && s[1] == 'x') {
    base = 16;
    s += 2;
  }

  while (*s) {
    int v;

    if (*s >= '0' && *s <= '9')
      v = *s - '0';
    else if (*s >= 'a' && *s <= 'f')
      v = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'F')
      v = *s - 'A' + 10;
    else
      break;

    x = x * base + v;
    s++;
  }

  return neg ? -x : x;
}

//Redondea hacia arriba hacia un multiplo de "a". SE usa al alinear secciones en el ELF en su construcciones. EJ:
//align_up(65, 8) ---> 72  alinea la seccion .text a 8 bytes 
static int
align_up(int x, int a)
{
  return (x + a - 1) & ~(a - 1);
}

static int
add_str(char *tab, int *psz, char *s)
{
  int off = *psz;
  int n = strlen(s) + 1;

  if (*psz + n >= MAX_STRTAB)
    die("string table too large");

  memmove(tab + *psz, s, n);
  *psz += n;
  return off;
}

static struct Sym *
find_sym(char *name)
{
  for (int i = 0; i < nsyms; i++) {
    if (streq(syms[i].name, name))
      return &syms[i];
  }
  return 0;
}

static struct Sym *
get_sym(char *name)
{
  struct Sym *s = find_sym(name);

  if (s)
    return s;

  if (nsyms >= MAX_SYM)
    die("too many symbols");

  s = &syms[nsyms++];
  memset(s, 0, sizeof(*s));
  copy_name(s->name, name);
  s->sec = SEC_UNDEF;
  s->value = 0;
  s->defined = 0;
  s->global = 0;
  s->outidx = 0;
  s->stroff = 0;
  return s;
}

static void
mark_global(char *name)
{
  struct Sym *s = get_sym(name);
  s->global = 1;
}

static int
sec_offset(void)
{
  if (cursec == SEC_TEXT)
    return textsz;
  if (cursec == SEC_RODATA)
    return rodatasz;
  if (cursec == SEC_DATA)
    return datasz;
  die("bad current section");
  return 0;
}

static void
define_label(char *name)
{
  struct Sym *s = get_sym(name);

  s->sec = cursec;
  s->value = sec_offset();
  s->defined = 1;
}

static void
emit_byte(int sec, uchar b)
{
  if (sec == SEC_TEXT) {
    if (textsz >= MAX_TEXT)
      die(".text too large");
    text[textsz++] = b;
  } else if (sec == SEC_RODATA) {
    if (rodatasz >= MAX_RODATA)
      die(".rodata too large");
    rodata[rodatasz++] = b;
  } else if (sec == SEC_DATA) {
    if (datasz >= MAX_DATA)
      die(".data too large");
    data[datasz++] = b;
  } else {
    die("bad section");
  }
}

static void
emit32_text(uint x)
{
  if (textsz + 4 > MAX_TEXT)
    die(".text too large");

  text[textsz++] = x & 0xff;
  text[textsz++] = (x >> 8) & 0xff;
  text[textsz++] = (x >> 16) & 0xff;
  text[textsz++] = (x >> 24) & 0xff;
}

static void
emit_word_data(int sec, uint x)
{
  emit_byte(sec, x & 0xff);
  emit_byte(sec, (x >> 8) & 0xff);
  emit_byte(sec, (x >> 16) & 0xff);
  emit_byte(sec, (x >> 24) & 0xff);
}

static void
emit_dword_data(int sec, uint64 x)
{
  for (int i = 0; i < 8; i++)
    emit_byte(sec, (x >> (8 * i)) & 0xff);
}

static void
add_reloc(int sec, uint off, uint type, char *sym, int addend)
{
  if (nrels >= MAX_REL)
    die("too many relocations");

  get_sym(sym);

  rels[nrels].sec = sec;
  rels[nrels].off = off;
  rels[nrels].type = type;
  rels[nrels].addend = addend;
  copy_name(rels[nrels].sym, sym);
  nrels++;
}

static int
regno(char *s)
{
  if (s[0] == 'x' && s[1] >= '0' && s[1] <= '9') {
    int n = parse_num(s + 1);
    if (n >= 0 && n <= 31)
      return n;
  }

  if (streq(s, "zero")) return 0;
  if (streq(s, "ra")) return 1;
  if (streq(s, "sp")) return 2;
  if (streq(s, "gp")) return 3;
  if (streq(s, "tp")) return 4;

  if (streq(s, "t0")) return 5;
  if (streq(s, "t1")) return 6;
  if (streq(s, "t2")) return 7;

  if (streq(s, "s0")) return 8;
  if (streq(s, "fp")) return 8;
  if (streq(s, "s1")) return 9;

  if (streq(s, "a0")) return 10;
  if (streq(s, "a1")) return 11;
  if (streq(s, "a2")) return 12;
  if (streq(s, "a3")) return 13;
  if (streq(s, "a4")) return 14;
  if (streq(s, "a5")) return 15;
  if (streq(s, "a6")) return 16;
  if (streq(s, "a7")) return 17;

  if (streq(s, "s2")) return 18;
  if (streq(s, "s3")) return 19;
  if (streq(s, "s4")) return 20;
  if (streq(s, "s5")) return 21;
  if (streq(s, "s6")) return 22;
  if (streq(s, "s7")) return 23;
  if (streq(s, "s8")) return 24;
  if (streq(s, "s9")) return 25;
  if (streq(s, "s10")) return 26;
  if (streq(s, "s11")) return 27;

  if (streq(s, "t3")) return 28;
  if (streq(s, "t4")) return 29;
  if (streq(s, "t5")) return 30;
  if (streq(s, "t6")) return 31;

  return -1;
}

static uint
enc_r(int funct7, int rs2, int rs1, int funct3, int rd, int opcode)
{
  return ((funct7 & 0x7f) << 25) |
         ((rs2 & 0x1f) << 20) |
         ((rs1 & 0x1f) << 15) |
         ((funct3 & 7) << 12) |
         ((rd & 0x1f) << 7) |
         (opcode & 0x7f);
}

static uint
enc_i(int imm, int rs1, int funct3, int rd, int opcode)
{
  return ((imm & 0xfff) << 20) |
         ((rs1 & 0x1f) << 15) |
         ((funct3 & 7) << 12) |
         ((rd & 0x1f) << 7) |
         (opcode & 0x7f);
}

static uint
enc_u(int imm20, int rd, int opcode)
{
  return ((imm20 & 0xfffff) << 12) |
         ((rd & 0x1f) << 7) |
         (opcode & 0x7f);
}

static uint
enc_jal_zero(int rd)
{
  return (rd & 0x1f) << 7 | 0x6f;
}

static int
next_arg(char **pp, char *out)
{
  char *p = *pp;
  int i = 0;

  p = skip_spaces(p);

  if (*p == ',') {
    p++;
    p = skip_spaces(p);
  }

  if (*p == 0)
    return 0;

  while (*p && *p != ',' && !is_space(*p)) {
    if (i < 63)
      out[i++] = *p;
    p++;
  }

  out[i] = 0;
  *pp = p;
  return 1;
}

static void
emit_li(int rd, int imm)
{
  if (imm >= -2048 && imm <= 2047) {
    emit32_text(enc_i(imm, 0, 0, rd, 0x13));
  } else {
    int hi = (imm + 0x800) >> 12;
    int lo = imm - (hi << 12);
    emit32_text(enc_u(hi, rd, 0x37));
    emit32_text(enc_i(lo, rd, 0, rd, 0x13));
  }
}

static void
emit_la(int rd, char *sym)
{
  uint off;

  off = textsz;
  emit32_text(enc_u(0, rd, 0x37));
  add_reloc(SEC_TEXT, off, R_RISCV_HI20, sym, 0);

  off = textsz;
  emit32_text(enc_i(0, rd, 0, rd, 0x13));
  add_reloc(SEC_TEXT, off, R_RISCV_LO12_I, sym, 0);
}

static void
emit_j(char *sym, int rd)
{
  uint off = textsz;
  emit32_text(enc_jal_zero(rd));
  add_reloc(SEC_TEXT, off, R_RISCV_JAL, sym, 0);
}

static void
parse_string_emit(char *p, int nul)
{
  p = skip_spaces(p);

  if (*p != '"')
    die("expected string");

  p++;

  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (*p == 'n')
        emit_byte(cursec, '\n');
      else if (*p == 't')
        emit_byte(cursec, '\t');
      else if (*p == 'r')
        emit_byte(cursec, '\r');
      else if (*p == '0')
        emit_byte(cursec, 0);
      else if (*p == '"')
        emit_byte(cursec, '"');
      else if (*p == '\\')
        emit_byte(cursec, '\\');
      else
        emit_byte(cursec, *p);

      if (*p)
        p++;
    } else {
      emit_byte(cursec, *p);
      p++;
    }
  }

  if (nul)
    emit_byte(cursec, 0);
}

static void
parse_directive(char *line)
{
  char op[64];
  char a[64];
  char *p = line;

  next_arg(&p, op);

  if (streq(op, ".text")) {
    cursec = SEC_TEXT;
    return;
  }

  if (streq(op, ".rodata")) {
    cursec = SEC_RODATA;
    return;
  }

  if (streq(op, ".data")) {
    cursec = SEC_DATA;
    return;
  }

  if (streq(op, ".globl") || streq(op, ".global")) {
    if (!next_arg(&p, a))
      die("missing symbol after .globl");
    mark_global(a);
    return;
  }

  if (streq(op, ".ascii")) {
    parse_string_emit(p, 0);
    return;
  }

  if (streq(op, ".asciz")) {
    parse_string_emit(p, 1);
    return;
  }

  if (streq(op, ".byte")) {
    while (next_arg(&p, a))
      emit_byte(cursec, parse_num(a) & 0xff);
    return;
  }

  if (streq(op, ".word")) {
    while (next_arg(&p, a))
      emit_word_data(cursec, parse_num(a));
    return;
  }

  if (streq(op, ".dword")) {
    while (next_arg(&p, a))
      emit_dword_data(cursec, (uint64)parse_num(a));
    return;
  }

  die("unknown directive");
}

static void
parse_inst(char *line)
{
  char op[64], a0[64], a1[64], a2[64];
  char *p = line;
  int rd, rs1, rs2, imm;

  if (!next_arg(&p, op))
    return;

  if (streq(op, "ecall")) {
    emit32_text(0x00000073);
    return;
  }

  if (streq(op, "ret")) {
    emit32_text(enc_i(0, 1, 0, 0, 0x67));
    return;
  }

  if (streq(op, "li")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1))
      die("bad li");
    rd = regno(a0);
    if (rd < 0)
      die("bad register in li");
    imm = parse_num(a1);
    emit_li(rd, imm);
    return;
  }

  if (streq(op, "mv")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1))
      die("bad mv");
    rd = regno(a0);
    rs1 = regno(a1);
    if (rd < 0 || rs1 < 0)
      die("bad register in mv");
    emit32_text(enc_i(0, rs1, 0, rd, 0x13));
    return;
  }

  if (streq(op, "addi")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1) || !next_arg(&p, a2))
      die("bad addi");
    rd = regno(a0);
    rs1 = regno(a1);
    imm = parse_num(a2);
    if (rd < 0 || rs1 < 0)
      die("bad register in addi");
    emit32_text(enc_i(imm, rs1, 0, rd, 0x13));
    return;
  }

  if (streq(op, "add")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1) || !next_arg(&p, a2))
      die("bad add");
    rd = regno(a0);
    rs1 = regno(a1);
    rs2 = regno(a2);
    if (rd < 0 || rs1 < 0 || rs2 < 0)
      die("bad register in add");
    emit32_text(enc_r(0x00, rs2, rs1, 0, rd, 0x33));
    return;
  }

  if (streq(op, "sub")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1) || !next_arg(&p, a2))
      die("bad sub");
    rd = regno(a0);
    rs1 = regno(a1);
    rs2 = regno(a2);
    if (rd < 0 || rs1 < 0 || rs2 < 0)
      die("bad register in sub");
    emit32_text(enc_r(0x20, rs2, rs1, 0, rd, 0x33));
    return;
  }

  if (streq(op, "lui")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1))
      die("bad lui");
    rd = regno(a0);
    imm = parse_num(a1);
    if (rd < 0)
      die("bad register in lui");
    emit32_text(enc_u(imm, rd, 0x37));
    return;
  }

  if (streq(op, "la")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1))
      die("bad la");
    rd = regno(a0);
    if (rd < 0)
      die("bad register in la");
    emit_la(rd, a1);
    return;
  }

  if (streq(op, "j")) {
    if (!next_arg(&p, a0))
      die("bad j");
    emit_j(a0, 0);
    return;
  }

  if (streq(op, "jal")) {
    if (!next_arg(&p, a0) || !next_arg(&p, a1))
      die("bad jal");
    rd = regno(a0);
    if (rd < 0)
      die("bad register in jal");
    emit_j(a1, rd);
    return;
  }

  die("unknown instruction");
}

static void
parse_line(char *line)
{
  char *p;
  char name[64];
  int i;

  remove_comment(line);
  trim_right(line);
  p = skip_spaces(line);

  if (*p == 0)
    return;

  while (1) {
    char *q = p;
    i = 0;

    while (*q && *q != ':' && !is_space(*q)) {
      if (i < 63)
        name[i++] = *q;
      q++;
    }

    name[i] = 0;

    if (*q == ':') {
      define_label(name);
      p = skip_spaces(q + 1);
      if (*p == 0)
        return;
    } else {
      break;
    }
  }

  if (*p == '.')
    parse_directive(p);
  else
    parse_inst(p);
}

static void
parse_source(void)
{
  char *p = src;

  while (*p) {
    char *line = p;

    while (*p && *p != '\n')
      p++;

    if (*p == '\n') {
      *p = 0;
      p++;
    }

    parse_line(line);
  }

  for (int i = 0; i < nsyms; i++) {
    if (!syms[i].defined)
      syms[i].global = 1;
  }
}

static int
read_all(char *path)
{
  int fd;
  int n;
  int total = 0;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  while ((n = read(fd, src + total, MAX_SRC - 1 - total)) > 0) {
    total += n;
    if (total >= MAX_SRC - 1)
      die("source file too large");
  }

  src[total] = 0;
  close(fd);
  return total;
}

static void
write_full(int fd, void *buf, int n)
{
  int r = write(fd, buf, n);
  if (r != n)
    die("write failed");
}

static void
pad_to(int fd, int *pos, int target)
{
  uchar zero = 0;

  while (*pos < target) {
    write_full(fd, &zero, 1);
    (*pos)++;
  }
}

static int
count_local_syms(void)
{
  int n = 1;

  for (int i = 0; i < nsyms; i++) {
    if (syms[i].defined && !syms[i].global)
      n++;
  }

  return n;
}

static void
assign_symbol_indexes(void)
{
  int idx = 1;

  strsz = 0;
  strtab[strsz++] = 0;

  for (int i = 0; i < nsyms; i++) {
    if (syms[i].defined && !syms[i].global) {
      syms[i].outidx = idx++;
      syms[i].stroff = add_str(strtab, &strsz, syms[i].name);
    }
  }

  for (int i = 0; i < nsyms; i++) {
    if (!syms[i].defined || syms[i].global) {
      syms[i].outidx = idx++;
      syms[i].stroff = add_str(strtab, &strsz, syms[i].name);
    }
  }
}

static struct Sym *
sym_by_name(char *name)
{
  struct Sym *s = find_sym(name);
  if (!s)
    die("internal missing symbol");
  return s;
}

static void
write_one_sym(int fd, struct Sym *s)
{
  struct Elf64_Sym es;

  memset(&es, 0, sizeof(es));
  es.st_name = s->stroff;
  es.st_info = ((s->global ? STB_GLOBAL : STB_LOCAL) << 4) | STT_NOTYPE;
  es.st_other = 0;
  es.st_shndx = s->defined ? s->sec : SEC_UNDEF;
  es.st_value = s->defined ? s->value : 0;
  es.st_size = 0;

  write_full(fd, &es, sizeof(es));
}

static void
write_symtab(int fd)
{
  struct Elf64_Sym zero;

  memset(&zero, 0, sizeof(zero));
  write_full(fd, &zero, sizeof(zero));

  for (int i = 0; i < nsyms; i++) {
    if (syms[i].defined && !syms[i].global)
      write_one_sym(fd, &syms[i]);
  }

  for (int i = 0; i < nsyms; i++) {
    if (!syms[i].defined || syms[i].global)
      write_one_sym(fd, &syms[i]);
  }
}

static void
write_relas(int fd)
{
  for (int i = 0; i < nrels; i++) {
    struct Elf64_Rela r;
    struct Sym *s = sym_by_name(rels[i].sym);

    memset(&r, 0, sizeof(r));
    r.r_offset = rels[i].off;
    r.r_info = ELF64_R_INFO(s->outidx, rels[i].type);
    r.r_addend = rels[i].addend;

    write_full(fd, &r, sizeof(r));
  }
}

static int
shname(char *name)
{
  return add_str(shstrtab, &shstrsz, name);
}

static void
emit_elf(char *out)
{
  int fd;
  int pos = 0;
  int noutsyms;
  int first_global;
  int nsec = 8;

  int name_text, name_rodata, name_data;
  int name_rela_text, name_symtab, name_strtab, name_shstrtab;

  int text_off, rodata_off, data_off;
  int rela_off, symtab_off, strtab_off, shstrtab_off, shoff;

  struct Elf64_Ehdr eh;
  struct Elf64_Shdr sh[8];

  assign_symbol_indexes();

  noutsyms = nsyms + 1;
  first_global = count_local_syms();

  shstrsz = 0;
  shstrtab[shstrsz++] = 0;

  name_text = shname(".text");
  name_rodata = shname(".rodata");
  name_data = shname(".data");
  name_rela_text = shname(".rela.text");
  name_symtab = shname(".symtab");
  name_strtab = shname(".strtab");
  name_shstrtab = shname(".shstrtab");

  text_off = align_up(sizeof(struct Elf64_Ehdr), 4);
  rodata_off = text_off + textsz;
  data_off = rodata_off + rodatasz;

  rela_off = align_up(data_off + datasz, 8);
  symtab_off = align_up(rela_off + nrels * sizeof(struct Elf64_Rela), 8);
  strtab_off = symtab_off + noutsyms * sizeof(struct Elf64_Sym);
  shstrtab_off = strtab_off + strsz;
  shoff = align_up(shstrtab_off + shstrsz, 8);

  memset(&eh, 0, sizeof(eh));
  eh.e_ident[0] = 0x7f;
  eh.e_ident[1] = 'E';
  eh.e_ident[2] = 'L';
  eh.e_ident[3] = 'F';
  eh.e_ident[4] = 2;
  eh.e_ident[5] = 1;
  eh.e_ident[6] = 1;
  eh.e_type = ET_REL;
  eh.e_machine = EM_RISCV;
  eh.e_version = EV_CURRENT;
  eh.e_entry = 0;
  eh.e_phoff = 0;
  eh.e_shoff = shoff;
  eh.e_flags = 0;
  eh.e_ehsize = sizeof(struct Elf64_Ehdr);
  eh.e_phentsize = 0;
  eh.e_phnum = 0;
  eh.e_shentsize = sizeof(struct Elf64_Shdr);
  eh.e_shnum = nsec;
  eh.e_shstrndx = 7;

  memset(sh, 0, sizeof(sh));

  sh[1].sh_name = name_text;
  sh[1].sh_type = SHT_PROGBITS;
  sh[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  sh[1].sh_offset = text_off;
  sh[1].sh_size = textsz;
  sh[1].sh_addralign = 4;

  sh[2].sh_name = name_rodata;
  sh[2].sh_type = SHT_PROGBITS;
  sh[2].sh_flags = SHF_ALLOC;
  sh[2].sh_offset = rodata_off;
  sh[2].sh_size = rodatasz;
  sh[2].sh_addralign = 1;

  sh[3].sh_name = name_data;
  sh[3].sh_type = SHT_PROGBITS;
  sh[3].sh_flags = SHF_ALLOC | SHF_WRITE;
  sh[3].sh_offset = data_off;
  sh[3].sh_size = datasz;
  sh[3].sh_addralign = 8;

  sh[4].sh_name = name_rela_text;
  sh[4].sh_type = SHT_RELA;
  sh[4].sh_offset = rela_off;
  sh[4].sh_size = nrels * sizeof(struct Elf64_Rela);
  sh[4].sh_link = 5;
  sh[4].sh_info = 1;
  sh[4].sh_addralign = 8;
  sh[4].sh_entsize = sizeof(struct Elf64_Rela);

  sh[5].sh_name = name_symtab;
  sh[5].sh_type = SHT_SYMTAB;
  sh[5].sh_offset = symtab_off;
  sh[5].sh_size = noutsyms * sizeof(struct Elf64_Sym);
  sh[5].sh_link = 6;
  sh[5].sh_info = first_global;
  sh[5].sh_addralign = 8;
  sh[5].sh_entsize = sizeof(struct Elf64_Sym);

  sh[6].sh_name = name_strtab;
  sh[6].sh_type = SHT_STRTAB;
  sh[6].sh_offset = strtab_off;
  sh[6].sh_size = strsz;
  sh[6].sh_addralign = 1;

  sh[7].sh_name = name_shstrtab;
  sh[7].sh_type = SHT_STRTAB;
  sh[7].sh_offset = shstrtab_off;
  sh[7].sh_size = shstrsz;
  sh[7].sh_addralign = 1;

  fd = open(out, O_CREATE | O_WRONLY);
  if (fd < 0)
    die("cannot create output file");

  write_full(fd, &eh, sizeof(eh));
  pos += sizeof(eh);

  pad_to(fd, &pos, text_off);
  write_full(fd, text, textsz);
  pos += textsz;

  pad_to(fd, &pos, rodata_off);
  write_full(fd, rodata, rodatasz);
  pos += rodatasz;

  pad_to(fd, &pos, data_off);
  write_full(fd, data, datasz);
  pos += datasz;

  pad_to(fd, &pos, rela_off);
  write_relas(fd);
  pos += nrels * sizeof(struct Elf64_Rela);

  pad_to(fd, &pos, symtab_off);
  write_symtab(fd);
  pos += noutsyms * sizeof(struct Elf64_Sym);

  pad_to(fd, &pos, strtab_off);
  write_full(fd, strtab, strsz);
  pos += strsz;

  pad_to(fd, &pos, shstrtab_off);
  write_full(fd, shstrtab, shstrsz);
  pos += shstrsz;

  pad_to(fd, &pos, shoff);
  write_full(fd, sh, sizeof(sh));

  close(fd);
}

int
main(int argc, char **argv)
{
  if (argc != 3) {
    fprintf(2, "usage: asxv6 input.s output.o\n");
    exit(1);
  }

  if (read_all(argv[1]) < 0) {
    fprintf(2, "asxv6: cannot open %s\n", argv[1]);
    exit(1);
  }

  parse_source();
  emit_elf(argv[2]);

  exit(0);
}