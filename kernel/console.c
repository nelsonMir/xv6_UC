// Console I/O por OpenSBI; cablea devsw[CONSOLE].\r
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"   // para struct sleeplock (lo requiere file.h)\r
#include "fs.h"          // para NDIRECT (lo requiere file.h)\r
#include "file.h"        // devsw[], CONSOLE\r
#include "proc.h"
#include "defs.h"

#define INPUT_BUF 128
#define C(x)      ((x)-'@')
#define BACKSPACE 0x100

// -------- OpenSBI (legacy) --------
static inline long sbi_call(long eid, long a0, long a1, long a2) {
  register long A0 asm("a0") = a0;
  register long A1 asm("a1") = a1;
  register long A2 asm("a2") = a2;
  register long A7 asm("a7") = eid;
  asm volatile("ecall" : "+r"(A0) : "r"(A1), "r"(A2), "r"(A7) : "memory");
  return A0;
}
static inline void sbi_putchar(int ch) { sbi_call(0x01, ch & 0xff, 0, 0); }
static inline int  sbi_getchar(void)   { return (int)sbi_call(0x02, 0, 0, 0); } // -1 si no hay\r

struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r, w, e; // read/write/edit\r
} cons;

void
consputc(int c)
{
  if(c == BACKSPACE){
    sbi_putchar('\b'); sbi_putchar(' '); sbi_putchar('\b');
  } else {
    if(c == '\n') sbi_putchar('\r');
    sbi_putchar(c);
  }
}

// write(fd, buf, n) -> consola\r
int
consolewrite(int user_src, uint64 src, int n)
{
  int i; char c;
  acquire(&cons.lock);
  for(i = 0; i < n; i++){
    if(either_copyin(&c, user_src, src+i, 1) == -1) break;
    consputc((unsigned char)c);
  }
  release(&cons.lock);
  return i;
}

// read(fd, buf, n) <- consola (sondeo OpenSBI, sin timer/IRQ)\r
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target = n;
  int c; char cbuf;

  acquire(&cons.lock);
  while(n > 0){
    while(cons.r == cons.w){
      int k = sbi_getchar(); // no bloqueante\r
      if (k >= 0) {
        if(k == '\r') k = '\n';
        if(cons.e - cons.r < INPUT_BUF){
          cons.buf[cons.e % INPUT_BUF] = (char)k;
          cons.e++;
          consputc(k); // eco\r
          if(k == '\n' || k == C('D') || cons.e == cons.r + INPUT_BUF){
            cons.w = cons.e;
            wakeup(&cons.r);
          }
        }
        break;
      }
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      // espera ocupada corta\r
      release(&cons.lock);
      for (volatile int i = 0; i < 20000; i++) { __asm__ volatile(""); }
      acquire(&cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF];

    if(c == C('D')){
      if(n < target) cons.r--;
      break;
    }

    cbuf = (char)c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1) break;
    dst++; --n;
    if(c == '\n') break;
  }
  release(&cons.lock);
  return target - n;
}

// Compatible con ISR si algún día enrutas UART MMIO\r
void
consoleintr(int c)
{
  acquire(&cons.lock);
  switch(c){
  case C('P'):
    procdump();
    break;
  case C('U'):
    while(cons.e != cons.w && cons.buf[(cons.e-1) % INPUT_BUF] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'):
  case '\x7f':
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e - cons.r < INPUT_BUF){
      if(c == '\r') c = '\n';
      cons.buf[cons.e % INPUT_BUF] = c;
      cons.e++;
      consputc(c);
      if(c == '\n' || c == C('D') || cons.e == cons.r + INPUT_BUF){
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");
  // *** Wiring a la tabla de dispositivos: esencial para /dev/console ***\r
  devsw[CONSOLE].read  = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
