//
// low-level driver routines for 16550a UART.
//
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
//
// *** Acceso MMIO via helpers con stride dinámico (1 u 4) y acceso 32-bit. ***
static int uart_stride = 4; // probamos en uartinit() y ajustamos

static inline volatile uint32 *uart_reg_ptr32(int reg)
{
  return (volatile uint32 *)(UART0 + (uart_stride * reg));
}

static inline unsigned char ReadReg(int reg)
{
  return (unsigned char)(*uart_reg_ptr32(reg) & 0xFF);
}

static inline void WriteReg(int reg, unsigned char v)
{
  *uart_reg_ptr32(reg) = (uint32)v;
}


// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define MCR 4                 // Modem Control Register
#define MCR_DTR   0x01        // Data Terminal Ready
#define MCR_RTS   0x02        // Request To Send
#define MCR_OUT2  0x08        // Habilita routing de IRQ en muchos 16550
#define MCR_LOOP  0x10        // loopback interno
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send
//para que la consola responda agrego unas macros
#define IIR 2                 // Interrupt Identification Register
#define IIR_NOPEND 0x01       // bit0=1 => no hay IRQ pendiente
#define SCR 7                 // Scratch Register (libre uso)

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c

void uartstart();

// -----------------------------
// Probar stride (1 vs 4) usando SCR.
// Devuelve 1 si OK con stride dado, 0 si no.
static int __attribute__((unused)) uart_try_stride(int stride)
{
  uart_stride = stride;

  unsigned char old_lcr = ReadReg(LCR);
  unsigned char old_scr = ReadReg(SCR);

  WriteReg(SCR, 0x5A);
  unsigned char v1 = ReadReg(SCR);
  WriteReg(SCR, 0xA5);
  unsigned char v2 = ReadReg(SCR);

  WriteReg(SCR, old_scr);
  WriteReg(LCR, old_lcr);

  return (v1 == 0x5A && v2 == 0xA5);
}


// Trae arriba el 16550 y deja la UART lista para RX por IRQ y TX por polling.
// Mantiene tu enfoque (stride dinámico + FIFO OFF mientras depuras).
void
uartinit(void)
{
  // 1) Detecta stride: intenta 4, si no 1.
  if (!uart_try_stride(4)) {
    if (!uart_try_stride(1)) {
      printf("UART: no se pudo detectar stride (1/4)\r\n");
    }
  }

  // 2) Config básica: 8N1, FIFO off (temporal), líneas de módem y OUT2.
  WriteReg(LCR, LCR_EIGHT_BITS);
  WriteReg(FCR, 0x00);                           // FIFO off (más simple depurar)
  WriteReg(MCR, MCR_DTR | MCR_RTS | MCR_OUT2);   // OUT2 habilita routing de IRQ

  // 3) Habilita SOLO RX por IRQ (TX lo haces por uartstart()/polling).
  WriteReg(IER, IER_RX_ENABLE);

  // 4) Init del spinlock y punteros de buffer TX
  initlock(&uart_tx_lock, "uart");
  uart_tx_w = uart_tx_r = 0;

  // 5) Debug rápido para verificar acceso a registros
  printf("UART init: stride=%d IIR=0x%x LSR=0x%x IER=0x%x\r\n",
       uart_stride, (int)ReadReg(IIR), (int)ReadReg(LSR), (int)ReadReg(IER));

}



// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, (unsigned char)c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      (void)ReadReg(IIR);
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    WriteReg(THR, (unsigned char)c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void
uartintr(void)
{
  printf("UART IRQ top: IIR=0x%x LSR=0x%x\r\n", (int)ReadReg(IIR), (int)ReadReg(LSR));

  // --- DEBUG: imprime el estado al entrar al IRQ
  unsigned char iir  = ReadReg(IIR);
  unsigned char lsr0 = ReadReg(LSR);
  printf("UART IRQ: IIR=0x%x LSR=0x%x\r\n", (int)iir, (int)lsr0);


  // bit0 = 1 => no hay IRQ pendiente (espurio)
  if (iir & 1)
    return;

  // RX: vacía todo lo disponible
  while (ReadReg(LSR) & LSR_RX_READY) {
    int c = ReadReg(RHR);
    consoleintr(c);
  }

  // TX: empuja más si hay
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}


// --- helper visible desde otros módulos ---
int uart_rx_ready(void) {
  return (ReadReg(LSR) & LSR_RX_READY) != 0;
}

// Habilita IRQ RX/ líneas de módem/8N1 en tiempo de ejecución (sin uartinit()).
void uart_enable_irq_runtime(void)
{
  // 8N1
  WriteReg(LCR, LCR_EIGHT_BITS);

  // Levanta DTR/RTS y OUT2 (OUT2 suele rutear IRQ del 16550)
  WriteReg(MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

  // Para depurar, desactiva FIFO (evita rarezas mientras ajustamos)
  WriteReg(FCR, 0x00);

  // Habilita interrupción por RX (opcionalmente también TX)
  WriteReg(IER, IER_RX_ENABLE /* | IER_TX_ENABLE */);
}


//------------------------------
//mas funciones de debug para hacer un eco en cruso + sonda LSR (para ver si llega algo al UART)
static inline int uart_kbhit(void){
  return (ReadReg(LSR) & LSR_RX_READY) != 0;
}

static inline int uart_getc_nb(void){
  if(uart_kbhit()) return ReadReg(RHR);
  return -1;
}

void uart_debug_poll(void){
  for (int i = 0; i < 16; i++){
    int c = uart_getc_nb();
    if (c < 0) break;
    // eco "duro" por hardware, para que veas tu tecla
    if (c == '\r') uartputc_sync('\n');
    uartputc_sync(c);
    // y además entrégalo al subsistema de consola/xv6
    consoleintr(c);
  }
}

// Devuelve 0 si pasó (recibió lo enviado), -1 si falló.
int uart_selftest(void)
{
  unsigned char lcr = ReadReg(LCR);
  unsigned char mcr = ReadReg(MCR);
  unsigned char ier = ReadReg(IER);
  unsigned char fcr = ReadReg(FCR);
  int ok = -1;

  // Silencia IRQs y limpia FIFOs
  // FIFO off temporalmente (simplifica mientras depuramos)
  WriteReg(FCR, 0x00);


  // Activa loopback y levanta líneas de módem
  WriteReg(MCR, (mcr | MCR_LOOP | MCR_DTR | MCR_RTS | MCR_OUT2));

  // Envía un patrón con muchos flancos
  WriteReg(THR, 0x55);

  // Espera a que aparezca en RHR
  for (volatile int i = 0; i < 1000000; i++) {
    unsigned char lsr2 = ReadReg(LSR);
    if (lsr2 & LSR_RX_READY) {
      int c = ReadReg(RHR);
      printf("UART selftest: LSR=0x%x, RHR=0x%x\n", lsr2, c);
      if (c == 0x55) ok = 0;
      break;
    }
  }

  // Restaura estado previo
  WriteReg(MCR, mcr);
  WriteReg(IER, ier);
  WriteReg(FCR, fcr);
  WriteReg(LCR, lcr);

  if (ok != 0)
    printf("UART selftest: FALLÓ (no se leyó eco en loopback)\n");
  return ok;
}
