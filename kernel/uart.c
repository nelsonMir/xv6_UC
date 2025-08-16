// kernel/uart.c — UART DW8250 (JH7110) con fallback a OpenSBI (RX por defecto)
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"

// 16550 / DW APB UART registers (índices), con reg-shift=2 => stride=4
#define RHR   0   // Receive Holding Reg     (DLAB=0, read)
#define THR   0   // Transmit Holding Reg    (DLAB=0, write)
#define IER   1   // Interrupt Enable        (DLAB=0)
#define FCR   2   // FIFO Control            (write)
#define IIR   2   // Interrupt ID            (read)
#define LCR   3   // Line Control
#define MCR   4   // Modem Control
#define LSR   5   // Line Status
#define MSR   6   // Modem Status
#define SCR   7   // Scratch
#define DLL   0   // Divisor Latch Low       (DLAB=1)
#define DLM   1   // Divisor Latch High      (DLAB=1)
#define USR  31   // DW8250: UART Status Register (offset 0x1f)

#define IER_RX_ENABLE  0x01
#define IER_TX_ENABLE  0x02

#define FCR_FIFO_ENABLE 0x01
#define FCR_CLEAR_RCVR  0x02
#define FCR_CLEAR_XMIT  0x04

#define LCR_EIGHT_BITS  0x03
#define LCR_BAUD_LATCH  0x80

#define MCR_DTR  0x01
#define MCR_RTS  0x02
#define MCR_OUT2 0x08

#define LSR_RX_READY 0x01
#define LSR_TX_IDLE  0x20

// IIR
#define IIR_NO_INT    0x01
#define IIR_IID(iir)  ((iir) & 0x0F)   // Busy Detect = 0x7 en DW8250

// TX buffer (como xv6)
#define UART_TX_BUF_SIZE 32
struct {
  struct spinlock lock;
  char buf[UART_TX_BUF_SIZE];
  uint w;
  uint r;
} uart_tx;

static volatile unsigned char *uart_base = (volatile unsigned char*)UART0;
static int stride = 1;                 // 1 u 4
static unsigned char cached_lcr = LCR_EIGHT_BITS;
static int uart_inited = 0;

// ------------ OpenSBI (LEGACY) fallback -------------
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

// ------------ helpers MMIO -------------
static inline unsigned char ReadReg(int idx) {
  return uart_base[idx * stride];
}
static inline void WriteReg(int idx, unsigned char v) {
  uart_base[idx * stride] = v;
}
static inline void dw_wait_not_busy(void) {
  for (int i = 0; i < 100000; i++) {
    if ((ReadReg(USR) & 0x01) == 0) return;
  }
}
static inline void set_lcr(unsigned char v) {
  dw_wait_not_busy();
  cached_lcr = v;
  WriteReg(LCR, v);
}
static int uart_try_stride(int s) {
  stride = s;
  unsigned char old = ReadReg(SCR);
  WriteReg(SCR, 0x5A);
  unsigned char ok = ReadReg(SCR);
  WriteReg(SCR, old);
  return ok == 0x5A;
}

// ------------ API -------------
void
uartinit(void)
{
  initlock(&uart_tx.lock, "uart");

  // Detecta stride 4 (reg-shift=2) o 1
  if (!uart_try_stride(4)) {
    uart_try_stride(1);
  }

  // Deshabilita IRQs mientras se configura
  WriteReg(IER, 0x00);

  // 8N1 (no tocamos divisor; asumimos baud del firmware)
  set_lcr(LCR_EIGHT_BITS);

  // FIFO on y limpia
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_CLEAR_RCVR | FCR_CLEAR_XMIT);

  // Levanta DTR/RTS y OUT2 (ruta IRQ)
  WriteReg(MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

  // Habilita SOLO RX por IRQ (evita ruido de THRE mientras afinamos)
  WriteReg(IER, IER_RX_ENABLE);

  uart_inited = 1;
}

static void
uartputc_sub(int c)
{
  // Espera a que TX esté libre por MMIO; si no, usa OpenSBI
  for (int i = 0; i < 100000; i++) {
    if (ReadReg(LSR) & LSR_TX_IDLE) {
      WriteReg(THR, c & 0xFF);
      return;
    }
  }
  sbi_putchar(c);
}

void
uartputc_sync(int c)
{
  if (!uart_inited) {
    sbi_putchar(c);
    return;
  }
  if (c == '\n')
    uartputc_sub('\r');
  uartputc_sub(c);
}

void
uartputc(int c)
{
  // en esta versión, transmitimos síncrono también para robustez
  uartputc_sync(c);
}

// no la marques static: coincide con defs.h
void
uartstart(void)
{
  // aquí ya usamos salida síncrona; nada que hacer
}

// Devuelve byte 0..255 o -1 si no hay
int
uartgetc(void)
{
  // PRIMERO intenta OpenSBI (así garantizamos RX aunque el MMIO no llegue)
  int ch = sbi_getchar();
  if (ch >= 0)
    return ch;

  // Si no hay por OpenSBI, intenta MMIO (por si ya afinas PLIC/8250)
  if (uart_inited && (ReadReg(LSR) & LSR_RX_READY))
    return ReadReg(RHR);

  return -1;
}

// ISR de UART (con busy-detect DW8250)
void
uartintr(void)
{
  unsigned char iir = ReadReg(IIR);

  // Si no hay IRQ pendiente, salir
  if (iir & IIR_NO_INT)
    return;

  unsigned char iid = IIR_IID(iir);

  // Busy-detect: limpia y reescribe LCR
  if (iid == 0x07) {
    (void)ReadReg(USR);
    WriteReg(LCR, cached_lcr);
    return;
  }

  // Recibir todo lo disponible por MMIO
  for (;;) {
    if ((ReadReg(LSR) & LSR_RX_READY) == 0)
      break;
    int c = ReadReg(RHR);
    consoleintr(c);
  }

  // TX: en esta versión, usamos síncrono fuera de la ISR
}

void
uart_debug_poll(void)
{
  int ch = uartgetc();
  if (ch >= 0)
    uartputc_sync(ch);
}
