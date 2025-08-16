#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// --- Prototipos del boot normal ---
void kinit(void);
void kvminit(void);
void kvminithart(void);
void procinit(void);
void trapinit(void);
void trapinithart(void);
void plicinit(void);
void plicinithart(void);
void userinit(void);
void binit(void);
void iinit(void);
void fileinit(void);
void virtio_disk_init(void);

// consola/uart
void consoleinit(void);
void uartinit(void);

// Símbolo débil (por si no existe en tu build)
void ramdisk_xv6_init(void) __attribute__((weak));

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
static inline int  sbi_getchar(void)   { return (int)sbi_call(0x02, 0, 0, 0); } // -1 si no hay

// -------- MMIO UART0 helpers (base=UART0 en memlayout.h) --------
static inline volatile unsigned char *uart_mmio(int stride) {
  return (volatile unsigned char *)(UART0); // 0x10000000\r
}
static inline unsigned char mmio_read(int stride, int idx) {
  volatile unsigned char *base = uart_mmio(stride);
  return base[idx * stride];
}
static inline void mmio_write(int stride, int idx, unsigned char v) {
  volatile unsigned char *base = uart_mmio(stride);
  base[idx * stride] = v;
}

// 16550 regs / bits
enum { RHR=0, THR=0, IER=1, FCR=2, IIR=2, LCR=3, MCR=4, LSR=5, MSR=6, SCR=7, USR=31 };
#define LSR_RX_READY 0x01
#define LSR_TX_IDLE  0x20
#define LCR_EIGHT_BITS 0x03
#define MCR_DTR  0x01
#define MCR_RTS  0x02
#define MCR_OUT2 0x08
#define FCR_FIFO_ENABLE 0x01
#define FCR_CLEAR_RCVR  0x02
#define FCR_CLEAR_XMIT  0x04
#define IER_RX_ENABLE   0x01
#define IER_TX_ENABLE   0x02

static void mmio_basic_init(int stride) {
  mmio_write(stride, LCR, LCR_EIGHT_BITS);
  mmio_write(stride, FCR, FCR_FIFO_ENABLE | FCR_CLEAR_RCVR | FCR_CLEAR_XMIT);
  mmio_write(stride, MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
  mmio_write(stride, IER, IER_RX_ENABLE);
}

static void mmio_putc_sync(int stride, int c) {
  for (int i = 0; i < 200000; i++) {
    if (mmio_read(stride, LSR) & LSR_TX_IDLE) {
      mmio_write(stride, THR, (unsigned char)c);
      return;
    }
  }
  sbi_putchar(c);
}

static int mmio_getc(int stride) {
  if (mmio_read(stride, LSR) & LSR_RX_READY)
    return mmio_read(stride, RHR);
  return -1;
}

static inline void tiny_delay(void) {
  for (volatile int i = 0; i < 40000; i++) { __asm__ volatile(""); }
}

static int phase_echo_sbi(void) {
  int got = 0;
  printf("[DIAG] Fase A: eco por OpenSBI (15s). Teclea algo...\r\n");
  for (int t = 0; t < 15000; t++) {
    int ch = sbi_getchar();
    if (ch >= 0) {
      got = 1;
      if (ch == '\n') sbi_putchar('\r');
      sbi_putchar(ch);
    }
    tiny_delay();
  }
  if (!got) printf("[DIAG] Fase A: no se leyó nada por SBI.\r\n");
  return got;
}

static int phase_echo_mmio_stride4(void) {
  int got = 0;
  printf("[DIAG] Fase B: eco por MMIO stride=4 (reg_shift=2) (15s).\r Teclea algo...\r\n");
  mmio_basic_init(4);
  for (int t = 0; t < 15000; t++) {
    int ch = mmio_getc(4);
    if (ch >= 0) {
      got = 1;
      if (ch == '\n') mmio_putc_sync(4, '\r');
      mmio_putc_sync(4, ch);
    }
    tiny_delay();
  }
  if (!got) {
    unsigned int iir = mmio_read(4, IIR);
    unsigned int lsr = mmio_read(4, LSR);
    unsigned int ier = mmio_read(4, IER);
    unsigned int mcr = mmio_read(4, MCR);
    unsigned int lcr = mmio_read(4, LCR);
    printf("[DIAG] Fase B: no llegó nada. IIR=0x%x LSR=0x%x IER=0x%x\r MCR=0x%x LCR=0x%x\r\n",
           iir, lsr, ier, mcr, lcr);
  }
  return got;
}

static int phase_echo_mmio_stride1(void) {
  int got = 0;
  printf("[DIAG] Fase C: eco por MMIO stride=1 (15s). Teclea algo...\r\n");
  mmio_basic_init(1);
  for (int t = 0; t < 15000; t++) {
    int ch = mmio_getc(1);
    if (ch >= 0) {
      got = 1;
      if (ch == '\n') mmio_putc_sync(1, '\r');
      mmio_putc_sync(1, ch);
    }
    tiny_delay();
  }
  if (!got) {
    unsigned int iir = mmio_read(1, IIR);
    unsigned int lsr = mmio_read(1, LSR);
    unsigned int ier = mmio_read(1, IER);
    unsigned int mcr = mmio_read(1, MCR);
    unsigned int lcr = mmio_read(1, LCR);
    printf("[DIAG] Fase C: no llegó nada. IIR=0x%x LSR=0x%x IER=0x%x\r MCR=0x%x LCR=0x%x\r\n",
           iir, lsr, ier, mcr, lcr);
  }
  return got;
}

int
main()
{
  // *** CLAVE: inicializa UART/CONSOLE antes de todo ***
  uartinit();
  consoleinit();

  printf("\nxv6-UC: diagnóstico de consola interactivo\r\n\n");

  // Fases de eco para confirmar rutas de entrada
  int ok_sbi  = phase_echo_sbi();
  int ok_m4   = phase_echo_mmio_stride4();
  int ok_m1   = phase_echo_mmio_stride1();

  if (!(ok_sbi || ok_m4 || ok_m1)) {
    printf("\n[DIAG] Ninguna ruta de entrada funcionó.\r Me quedo en eco por SBI...\r\n");
    for(;;){
      int ch = sbi_getchar();
      if (ch >= 0) {
        if (ch == '\n') sbi_putchar('\r');
        sbi_putchar(ch);
      }
      tiny_delay();
    }
  }

  printf("\n[DIAG] ¡Alguna ruta funcionó!\r Continuando con el boot normal...\r\n");

  // ---------- Boot normal de xv6 ----------
  kinit();
  kvminit();
  kvminithart();
  procinit();
  trapinit();
  trapinithart();
  plicinit();
  plicinithart();
  binit();
  iinit();
  fileinit();
  if (ramdisk_xv6_init) ramdisk_xv6_init();
  virtio_disk_init();
  userinit();
  scheduler();
}
