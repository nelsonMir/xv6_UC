#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "stat.h"   // añade esto arriba de exec.



static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x1)
      perm = PTE_X;
    if(flags & 0x2)
      perm |= PTE_W;
    return perm;
}

int
exec(char *path, char **argv)
{
  #if DBG_EXEC
  printf("exec: intentando abrir %s\r\n", path);
  #endif

  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    printf("exec: namei FAIL for \"%s\"\r\n", path);
    return -1;
  }
  
  ilock(ip);
  // <<< LOG seguro sin acceder a ip->campos >>>
  struct stat st;
  stati(ip, &st);
  #if DBG_EXEC
  printf("exec: namei OK ino=%d type=%d nlink=%d size=%ld\r\n",
         st.ino, st.type, st.nlink, st.size);
  #endif


  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf)){
    printf("exec: no se pudo leer encabezado ELF\r\n");
    goto bad;
  }
    
 if(elf.magic != ELF_MAGIC) {
    printf("exec: encabezado ELF invalido: 0x%x\r\n", elf.magic);
    goto bad;
  }

  #if DBG_EXEC
   printf("exec: ELF ok, entry=0x%ld, phoff=%ld, phnum=%d\r\n", elf.entry, elf.phoff, elf.phnum);
   #endif

  if((pagetable = proc_pagetable(p)) == 0) {
    printf("exec: fallo al crear pagetable\r\n");
    goto bad;
  }

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)){
      printf("exec: fallo al leer ph %d\r\n", i);
      goto bad;
    }
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz){
      printf("exec: memsz < filesz en ph %d\r\n", i);
      goto bad;
    }
    if(ph.vaddr + ph.memsz < ph.vaddr){
      printf("exec: overflow en vaddr + memsz en ph %d\r\n", i);
      goto bad;
    }
    if(ph.vaddr % PGSIZE != 0){
      printf("exec: vaddr no alineado en ph %d\r\n", i);
      goto bad;
    }

    #if DBG_EXEC
    printf("exec: segmento %d → va=0x%ld, filesz=%ld, memsz=%ld, flags=0x%x, off=%ld\r\n",
           i, ph.vaddr, ph.filesz, ph.memsz, ph.flags, ph.off);
    #endif

    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0) {
      printf("exec: uvmalloc fallo en ph %d\r\n", i);
      goto bad;
    }
    sz = sz1;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0){
      printf("exec: loadseg fallo en ph %d\r\n", i);
      goto bad;
    }
  }

  #if DBG_EXEC
  printf("exec: segmentos cargados, preparando pila...\r\n");
  #endif

  iunlockput(ip);
  end_op();
  #if DBG_EXEC
  printf("exec: success, epc=0x%lx sp=0x%lx\r\n", p->trapframe->epc, p->trapframe->sp);
  #endif
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W)) == 0){
    printf("exec: uvmalloc fallo al reservar pila\r\n");
    goto bad;
  }
  sz = sz1;
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK*PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
   for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) {
      printf("exec: demasiados argumentos\r\n");
      goto bad;
    }
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase){
      printf("exec: pila desbordada\r\n");
      goto bad;
    }
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0){
      printf("exec: fallo copyout de argumento %ld\r\n", argc);
      goto bad;
    }
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
   if(sp < stackbase){
    printf("exec: pila desbordada en argv[]\r\n");
    goto bad;
  }
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0){
    printf("exec: fallo copyout de ustack\r\n");
    goto bad;
  }
  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  #if DBG_EXEC
  printf("exec: listo para saltar a usuario (epc=0x%ld sp=0x%ld argc=%ld)\r\n", p->trapframe->epc, sp, argc);
  #endif 
  
  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  printf("exec: fallo general\r\n");
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
