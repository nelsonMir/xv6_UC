#include "kernel/types.h"

#include "elf.h"
#include "xv6_elf.h"

int
xv6_tcc_check_elf_abi(void)
{
  /*Se comprueba que las estructuras mantienen la disposición
  requerida por el formato ELF64*/

  if(sizeof(Elf64_Ehdr) != 64)
    return -1;

  if(sizeof(Elf64_Shdr) != 64)
    return -1;

  if(sizeof(Elf64_Sym) != 24)
    return -1;

  if(sizeof(Elf64_Rela) != 24)
    return -1;

  // Se comprueban las constantes utilizadas para objetos RISC-V

  if(ET_REL != 1)
    return -1;

  if(EM_RISCV != 243)
    return -1;

  if(ELFCLASS64 != 2)
    return -1;

  if(ELFDATA2LSB != 1)
    return -1;

  if(EV_CURRENT != 1)
    return -1;

  return 0;
}