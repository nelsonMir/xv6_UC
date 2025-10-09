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
static int uart_stride = 4; // se autodetecta en uartinit()

static inline volatile unsigned char *uart_reg_ptr8(int reg) {
  return (volatile unsigned char *)(UART0 + (uart_stride * reg));
}
static inline unsigned char ReadReg(int reg) { return *uart_reg_ptr8(reg); }
static inline void WriteReg(int reg, unsigned char v) { *uart_reg_ptr8(reg) = v; }


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
// DW-apb-uart: Uart Status Register (necesario para limpiar Busy Detect, etc.)
#define USR 31
 

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]
// --- CR/LF coalescing state (para evitar dobles saltos y "extras")
static int crlf_state = 0;  // 0: normal, 1: último fue '\r', 2: último fue '\n'

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

#if 0
// Descubre el ID real del UART en el PLIC probando una IRQ segura (THRE).
static int uart_irq_id = UART0_IRQ;  // valor por defecto; se corrige si detectamos otro

static void uart_force_thre_irq_on(void) {
  // Habilita ETBEI (bit1) temporalmente para generar THRE interrupt
  unsigned char ier = ReadReg(IER);
  ier |= 0x02;                 // ETBEI
  WriteReg(IER, ier);
}

static void uart_force_thre_irq_off(void) {
  unsigned char ier = ReadReg(IER);
  ier &= (unsigned char)~0x02; // limpia ETBEI, dejamos solo RX si estaba
  WriteReg(IER, ier);
}

static int uart_probe_plic_id(void)
{
  // Candidatos típicos en JH7110; ajusta/añade si hace falta
  const int cand[] = { 32, 33, 34, 35, 36, 37, 38, 39 };
  int found = -1;

  // Asegura THR vacío y fuerza THRE-IRQ
  (void)ReadReg(LSR);        // lee LSR para sincronizar
  uart_force_thre_irq_on();

  // Bucle de prueba: habilita SOLO un ID a la vez y mira plic_claim()
  for (unsigned i = 0; i < sizeof(cand)/sizeof(cand[0]); i++) {
    plic_disable_all_first64_extern();
    plic_enable_only_extern(cand[i]);

    // pequeña espera activa + prueba de claim
    for (volatile int spin = 0; spin < 200000; spin++) {
      int id = plic_claim();
      if (id) {
        printf("UART PLIC auto-detect: claim id=%d (cand=%d)\n", id, cand[i]);
        plic_complete(id);
        if (id == cand[i]) { found = id; }
        break;
      }
    }
    if (found != -1) break;
  }

  uart_force_thre_irq_off();
  // Restablece a solo RX
  WriteReg(IER, 0x01);
  return found;
}
#endif

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

  // 2) Deshabilita IRQs
  WriteReg(IER, 0x00);

  // 3) 8N1 y FIFO (OFF temporalmente para depurar RX claro y evitar rarezas)
  WriteReg(LCR, LCR_EIGHT_BITS);    // DLAB=0, 8N1
  WriteReg(FCR, 0x00);              // FIFO OFF (temporal mientras depuras)

  // 4) OUT2 + DTR/RTS para ruteo de IRQ en 16550 compatibles
  WriteReg(MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

  // 5) Sanea estado
  (void)ReadReg(USR);
  (void)ReadReg(LSR);
  while (ReadReg(LSR) & LSR_RX_READY) (void)ReadReg(RHR);

  // 6) Habilita IRQ de RX **y también TX**
  WriteReg(IER, IER_RX_ENABLE | IER_TX_ENABLE);  // <- clave para que el TX avance

  // 7) Init del spinlock y punteros de buffer TX
  initlock(&uart_tx_lock, "uart");
  uart_tx_w = uart_tx_r = 0;

  // 8) Debug rápido
  printf("UART init: stride=%d IIR=0x%x LSR=0x%x IER=0x%x\n",
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
int uartgetc(void)
{
  if (ReadReg(LSR) & LSR_RX_READY) {
    int c = ReadReg(RHR) & 0xff;
    if (c == '\r') c = '\n';
    return c;
  }
  return -1;
}


// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void uartintr(void)
{
  for (;;) {
    unsigned char iir = ReadReg(IIR);
    if (iir & IIR_NOPEND)
      break;                              // no hay IRQ pendiente

    unsigned char cause = (iir >> 1) & 0x7;  // 16550: bits 3:1
    switch (cause) {
      case 0x3: // 0b011: Receiver Line Status (LSR)
        (void)ReadReg(LSR);               // leer LSR limpia la causa
        break;

      case 0x2: // 0b010: Received Data Available
      case 0x6: // 0b110: Character Timeout (si FIFO ON)
        while (ReadReg(LSR) & LSR_RX_READY) {
          int ch = ReadReg(RHR) & 0xff;

          // --- CR/LF normalización + coalescing ---
          if (ch == '\r' || ch == '\n') {
            // si ya vimos el par opuesto justo antes, descarta este
            if ((crlf_state == 1 && ch == '\n') ||
                (crlf_state == 2 && ch == '\r')) {
              crlf_state = 0;     // consumimos la pareja CRLF/LFCR
              continue;           // descarta el duplicado
            }
            crlf_state = (ch == '\r') ? 1 : 2;
            ch = '\n';            // normaliza a '\n'
          } else {
            crlf_state = 0;
          }

          consoleintr(ch);
        }
        break;

      case 0x1: // 0b001: THR Empty (TX listo)
        acquire(&uart_tx_lock);
        uartstart();              // empuja más bytes del buffer TX
        release(&uart_tx_lock);
        break;

      default:
        // DW-apb-uart: limpia condiciones ruidosas
        (void)ReadReg(USR);
        (void)ReadReg(LSR);
        if (ReadReg(LSR) & LSR_RX_READY) (void)ReadReg(RHR);
        break;
    }
  }
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
