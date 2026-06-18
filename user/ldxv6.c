#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAX_OBJS     8
#define MAX_FILE     32768
#define MAX_OUT      65536
#define MAX_GLOBAL   256

#define EI_NIDENT    16

#define ET_REL       1
#define ET_EXEC      2
#define EM_RISCV     243
#define EV_CURRENT   1

#define PT_LOAD      1

#define PF_X         1
#define PF_W         2
#define PF_R         4

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4

#define SHN_UNDEF    0

#define STB_LOCAL    0
#define STB_GLOBAL   1

#define R_RISCV_NONE      0
#define R_RISCV_JAL       17
#define R_RISCV_HI20      26
#define R_RISCV_LO12_I    27

#define FILE_LOAD_OFF 0x1000

#define ELF64_R_SYM(info)  ((uint)((info) >> 32))
#define ELF64_R_TYPE(info) ((uint)((info) & 0xffffffff))
#define ELF64_ST_BIND(i)   ((i) >> 4)

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

struct Elf64_Phdr {
  uint   p_type;
  uint   p_flags;
  uint64 p_offset;
  uint64 p_vaddr;
  uint64 p_paddr;
  uint64 p_filesz;
  uint64 p_memsz;
  uint64 p_align;
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

struct Obj {
  char name[64];

  uchar file[MAX_FILE];
  int filesz;

  struct Elf64_Ehdr *eh;
  struct Elf64_Shdr *sh;

  char *shstr;

  int text_sec;
  int rodata_sec;
  int data_sec;
  int rela_text_sec;
  int symtab_sec;
  int strtab_sec;

  uchar *text;
  uchar *rodata;
  uchar *data;

  uint text_size;
  uint rodata_size;
  uint data_size;

  uint text_base;
  uint rodata_base;
  uint data_base;

  struct Elf64_Sym *symtab;
  int nsym;
  char *strtab;

  struct Elf64_Rela *rela_text;
  int nrela_text;
};

struct Global {
  char name[64];
  uint64 addr;
};

static struct Obj objs[MAX_OBJS];
static int nobjs;

static struct Global globals[MAX_GLOBAL];
static int nglobals;

static uchar outbuf[MAX_OUT];
static uint outsz;

static void
die(char *s)
{
  fprintf(2, "ldxv6: %s\n", s);
  exit(1);
}

static int
streq(char *a, char *b)
{
  return strcmp(a, b) == 0;
}

static void
copy_name(char *dst, char *src)
{
  int i = 0;

  while (src[i] && i < 63) {
    dst[i] = src[i];
    i++;
  }

  dst[i] = 0;
}

static uint
align_up(uint x, uint a)
{
  return (x + a - 1) & ~(a - 1);
}

static int
read_all(char *path, uchar *buf)
{
  int fd;
  int n;
  int total = 0;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  while ((n = read(fd, buf + total, MAX_FILE - total)) > 0) {
    total += n;

    if (total >= MAX_FILE)
      die("input object too large");
  }

  close(fd);
  return total;
}

static void
write_full(int fd, void *buf, int n)
{
  int r;

  r = write(fd, buf, n);
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

static char *
sec_name(struct Obj *o, int idx)
{
  return o->shstr + o->sh[idx].sh_name;
}

static void
load_obj(char *path)
{
  struct Obj *o;

  if (nobjs >= MAX_OBJS)
    die("too many input objects");

  o = &objs[nobjs++];
  memset(o, 0, sizeof(*o));
  copy_name(o->name, path);

  o->filesz = read_all(path, o->file);
  if (o->filesz < 0)
    die("cannot open input object");

  if (o->filesz < sizeof(struct Elf64_Ehdr))
    die("bad ELF object");

  o->eh = (struct Elf64_Ehdr *)o->file;

  if (o->eh->e_ident[0] != 0x7f ||
      o->eh->e_ident[1] != 'E' ||
      o->eh->e_ident[2] != 'L' ||
      o->eh->e_ident[3] != 'F')
    die("not an ELF file");

  if (o->eh->e_type != ET_REL)
    die("input is not ET_REL");

  if (o->eh->e_machine != EM_RISCV)
    die("input is not RISC-V ELF");

  if (o->eh->e_shoff == 0 || o->eh->e_shnum == 0)
    die("object has no section table");

  o->sh = (struct Elf64_Shdr *)(o->file + o->eh->e_shoff);

  if (o->eh->e_shstrndx >= o->eh->e_shnum)
    die("bad shstrndx");

  o->shstr = (char *)(o->file + o->sh[o->eh->e_shstrndx].sh_offset);

  o->text_sec = -1;
  o->rodata_sec = -1;
  o->data_sec = -1;
  o->rela_text_sec = -1;
  o->symtab_sec = -1;
  o->strtab_sec = -1;

  for (int i = 0; i < o->eh->e_shnum; i++) {
    char *n = sec_name(o, i);

    if (streq(n, ".text"))
      o->text_sec = i;
    else if (streq(n, ".rodata"))
      o->rodata_sec = i;
    else if (streq(n, ".data"))
      o->data_sec = i;
    else if (streq(n, ".rela.text"))
      o->rela_text_sec = i;
    else if (streq(n, ".symtab"))
      o->symtab_sec = i;
    else if (streq(n, ".strtab"))
      o->strtab_sec = i;
  }

  if (o->text_sec < 0)
    die("missing .text");

  if (o->symtab_sec < 0)
    die("missing .symtab");

  if (o->strtab_sec < 0)
    die("missing .strtab");

  o->text = o->file + o->sh[o->text_sec].sh_offset;
  o->text_size = o->sh[o->text_sec].sh_size;

  if (o->rodata_sec >= 0) {
    o->rodata = o->file + o->sh[o->rodata_sec].sh_offset;
    o->rodata_size = o->sh[o->rodata_sec].sh_size;
  }

  if (o->data_sec >= 0) {
    o->data = o->file + o->sh[o->data_sec].sh_offset;
    o->data_size = o->sh[o->data_sec].sh_size;
  }

  o->symtab = (struct Elf64_Sym *)(o->file + o->sh[o->symtab_sec].sh_offset);
  o->nsym = o->sh[o->symtab_sec].sh_size / sizeof(struct Elf64_Sym);

  o->strtab = (char *)(o->file + o->sh[o->strtab_sec].sh_offset);

  if (o->rela_text_sec >= 0) {
    o->rela_text = (struct Elf64_Rela *)(o->file + o->sh[o->rela_text_sec].sh_offset);
    o->nrela_text = o->sh[o->rela_text_sec].sh_size / sizeof(struct Elf64_Rela);
  }
}

static char *
sym_name(struct Obj *o, int idx)
{
  return o->strtab + o->symtab[idx].st_name;
}

static uint64
section_base(struct Obj *o, int shndx)
{
  if (shndx == o->text_sec)
    return o->text_base;

  if (shndx == o->rodata_sec)
    return o->rodata_base;

  if (shndx == o->data_sec)
    return o->data_base;

  die("unsupported symbol section");
  return 0;
}

static void
add_global(char *name, uint64 addr)
{
  for (int i = 0; i < nglobals; i++) {
    if (streq(globals[i].name, name))
      die("duplicate global symbol");
  }

  if (nglobals >= MAX_GLOBAL)
    die("too many global symbols");

  copy_name(globals[nglobals].name, name);
  globals[nglobals].addr = addr;
  nglobals++;
}

static uint64
find_global(char *name)
{
  for (int i = 0; i < nglobals; i++) {
    if (streq(globals[i].name, name))
      return globals[i].addr;
  }

  fprintf(2, "ldxv6: undefined symbol: %s\n", name);
  exit(1);
}

static uint64
symbol_addr(struct Obj *o, int symidx)
{
  struct Elf64_Sym *s;
  char *name;

  if (symidx <= 0 || symidx >= o->nsym)
    die("bad symbol index");

  s = &o->symtab[symidx];
  name = sym_name(o, symidx);

  if (s->st_shndx == SHN_UNDEF)
    return find_global(name);

  return section_base(o, s->st_shndx) + s->st_value;
}

static void
assign_layout(void)
{
  uint cur = 0;

  for (int i = 0; i < nobjs; i++) {
    objs[i].text_base = cur;
    cur += objs[i].text_size;
  }

  cur = align_up(cur, 8);

  for (int i = 0; i < nobjs; i++) {
    objs[i].rodata_base = cur;
    cur += objs[i].rodata_size;
  }

  cur = align_up(cur, 8);

  for (int i = 0; i < nobjs; i++) {
    objs[i].data_base = cur;
    cur += objs[i].data_size;
  }

  outsz = cur;

  if (outsz >= MAX_OUT)
    die("output too large");
}

static void
build_globals(void)
{
  for (int i = 0; i < nobjs; i++) {
    struct Obj *o = &objs[i];

    for (int j = 1; j < o->nsym; j++) {
      struct Elf64_Sym *s = &o->symtab[j];

      if (s->st_shndx == SHN_UNDEF)
        continue;

      if (ELF64_ST_BIND(s->st_info) == STB_GLOBAL) {
        uint64 addr = section_base(o, s->st_shndx) + s->st_value;
        add_global(sym_name(o, j), addr);
      }
    }
  }
}

static uint
get32(uint off)
{
  uint x = 0;

  x |= outbuf[off];
  x |= outbuf[off + 1] << 8;
  x |= outbuf[off + 2] << 16;
  x |= outbuf[off + 3] << 24;

  return x;
}

static void
put32(uint off, uint x)
{
  outbuf[off] = x & 0xff;
  outbuf[off + 1] = (x >> 8) & 0xff;
  outbuf[off + 2] = (x >> 16) & 0xff;
  outbuf[off + 3] = (x >> 24) & 0xff;
}

static void
copy_sections(void)
{
  memset(outbuf, 0, sizeof(outbuf));

  for (int i = 0; i < nobjs; i++) {
    struct Obj *o = &objs[i];

    if (o->text_size)
      memmove(outbuf + o->text_base, o->text, o->text_size);

    if (o->rodata_size)
      memmove(outbuf + o->rodata_base, o->rodata, o->rodata_size);

    if (o->data_size)
      memmove(outbuf + o->data_base, o->data, o->data_size);
  }
}

static void
patch_hi20(uint off, uint64 value)
{
  uint inst;
  uint hi20;

  hi20 = (value + 0x800) >> 12;
  inst = get32(off);
  inst = (inst & 0x00000fff) | ((hi20 & 0xfffff) << 12);
  put32(off, inst);
}

static void
patch_lo12_i(uint off, uint64 value)
{
  uint inst;
  uint lo12;

  lo12 = value & 0xfff;
  inst = get32(off);
  inst = (inst & 0x000fffff) | ((lo12 & 0xfff) << 20);
  put32(off, inst);
}

static void
patch_jal(uint off, int value)
{
  uint inst;
  uint imm;

  if (value < -1048576 || value > 1048574)
    die("jal target out of range");

  if (value & 1)
    die("jal target not aligned");

  imm = (uint)value;
  inst = get32(off);

  inst &= 0x00000fff;
  inst |= ((imm >> 20) & 0x1) << 31;
  inst |= ((imm >> 1) & 0x3ff) << 21;
  inst |= ((imm >> 11) & 0x1) << 20;
  inst |= ((imm >> 12) & 0xff) << 12;

  put32(off, inst);
}

static void
apply_relocations(void)
{
  for (int i = 0; i < nobjs; i++) {
    struct Obj *o = &objs[i];

    for (int r = 0; r < o->nrela_text; r++) {
      struct Elf64_Rela *rel = &o->rela_text[r];
      uint type = ELF64_R_TYPE(rel->r_info);
      uint symidx = ELF64_R_SYM(rel->r_info);
      uint outoff = o->text_base + rel->r_offset;
      uint64 saddr = symbol_addr(o, symidx);
      uint64 val = saddr + rel->r_addend;

      if (type == R_RISCV_NONE) {
        continue;
      } else if (type == R_RISCV_HI20) {
        patch_hi20(outoff, val);
      } else if (type == R_RISCV_LO12_I) {
        patch_lo12_i(outoff, val);
      } else if (type == R_RISCV_JAL) {
        int pc_rel = (int)(val - outoff);
        patch_jal(outoff, pc_rel);
      } else {
        fprintf(2, "ldxv6: unsupported relocation type %d\n", type);
        exit(1);
      }
    }
  }
}

static void
emit_exec(char *out)
{
  struct Elf64_Ehdr eh;
  struct Elf64_Phdr ph;
  int fd;
  int pos = 0;
  uint64 entry;

  entry = find_global("_start");

  memset(&eh, 0, sizeof(eh));
  eh.e_ident[0] = 0x7f;
  eh.e_ident[1] = 'E';
  eh.e_ident[2] = 'L';
  eh.e_ident[3] = 'F';
  eh.e_ident[4] = 2;
  eh.e_ident[5] = 1;
  eh.e_ident[6] = 1;

  eh.e_type = ET_EXEC;
  eh.e_machine = EM_RISCV;
  eh.e_version = EV_CURRENT;
  eh.e_entry = entry;
  eh.e_phoff = sizeof(struct Elf64_Ehdr);
  eh.e_shoff = 0;
  eh.e_flags = 0;
  eh.e_ehsize = sizeof(struct Elf64_Ehdr);
  eh.e_phentsize = sizeof(struct Elf64_Phdr);
  eh.e_phnum = 1;
  eh.e_shentsize = 0;
  eh.e_shnum = 0;
  eh.e_shstrndx = 0;

  memset(&ph, 0, sizeof(ph));
  ph.p_type = PT_LOAD;
  ph.p_flags = PF_R | PF_W | PF_X;
  ph.p_offset = FILE_LOAD_OFF;
  ph.p_vaddr = 0;
  ph.p_paddr = 0;
  ph.p_filesz = outsz;
  ph.p_memsz = outsz;
  ph.p_align = 0x1000;

  unlink(out);

  fd = open(out, O_CREATE | O_WRONLY);
  if (fd < 0)
    die("cannot create output executable");

  write_full(fd, &eh, sizeof(eh));
  pos += sizeof(eh);

  write_full(fd, &ph, sizeof(ph));
  pos += sizeof(ph);

  pad_to(fd, &pos, FILE_LOAD_OFF);

  write_full(fd, outbuf, outsz);
  pos += outsz;

  close(fd);
}

static void
usage(void)
{
  fprintf(2, "usage:\n");
  fprintf(2, "  ldxv6 input.o output\n");
  fprintf(2, "  ldxv6 input.o [input2.o ...] -o output\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  char *out = 0;
  int input_end;

  if (argc < 3)
    usage();

  if (argc == 3) {
    out = argv[2];
    input_end = 2;
  } else {
    if (!streq(argv[argc - 2], "-o"))
      usage();

    out = argv[argc - 1];
    input_end = argc - 2;
  }

  for (int i = 1; i < input_end; i++)
    load_obj(argv[i]);

  if (nobjs == 0)
    usage();

  assign_layout();
  build_globals();
  copy_sections();
  apply_relocations();
  emit_exec(out);

  exit(0);
}