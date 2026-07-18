#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

/*
  ================================================================
  Framebuffer console para xv6 
  ================================================================
 
  Salida:
    - caracteres ASCII imprimibles U+0020 hasta U+007E;
    - salto de línea
    - retorno de carro
    - tabulador de 8 columnas
    - backspace
    - salto automático de línea
    - scroll de una fila de texto
 
  La entrada de teclado continúa llegando por UART
 
  La fuente base es 8x8. Cada fila se duplica verticalmente para
  obtener caracteres de 8x16.
 */

#define FONT_SOURCE_WIDTH        8U
#define FONT_SOURCE_HEIGHT       8U
#define FBCONSOLE_CHAR_WIDTH     8U
#define FBCONSOLE_CHAR_HEIGHT    16U

#define FBCONSOLE_COLS \
  (HDMI_FB_WIDTH / FBCONSOLE_CHAR_WIDTH)

#define FBCONSOLE_ROWS \
  (HDMI_FB_HEIGHT / FBCONSOLE_CHAR_HEIGHT)

#define FBCONSOLE_TAB_WIDTH      8U

#define FBCONSOLE_BACKGROUND     0x00000000U  // negro
#define FBCONSOLE_FOREGROUND     0x00ffffffU  // blanco

/*
  Tabla ASCII básica 8x8.
 
  el bit 0 se interpreta como el pixel izquierdo de cada fila.

 */
static const uint8 font8x8_basic[128][8] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
  { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
  { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
  { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
  { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
  { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
  { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
  { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
  { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
  { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
  { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
  { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
  { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
  { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
  { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
  { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
  { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
  { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
  { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
  { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
  { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
  { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
  { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
  { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
  { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
  { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
  { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
  { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
  { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
  { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
  { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
  { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
  { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
  { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
  { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
  { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
  { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
  { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
  { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
  { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
  { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
  { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
  { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
  { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
  { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
  { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
  { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
  { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
  { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
  { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
  { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
  { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
  { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
  { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
  { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
  { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
  { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
  { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
  { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
  { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
  { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
  { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
  { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
  { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
  { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},   // U+0065 (e)
  { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
  { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
  { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
  { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
  { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
  { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
  { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
  { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
  { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
  { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
  { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
  { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
  { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
  { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
  { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
  { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
  { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
  { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
  { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
  { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
  { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
  { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
  { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
  { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
  { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
};

/*numero maximo de parámetros que se acepta dentro de una secuencia ANSI CSI ej:
ESC[12;40H --> tiene 2 parámetros: fila 12, columna 40
esto es  necesario para la versión de la placa porque se utiliza para mandar ordenes. Es deecir, en vez de mandar 
texto a secas, una secuencia ansi se usa para hacer operaciones o para indicar operaciones, por ejemplo: 
un programa puede mandar varias cosass:
write(1, "Hola", 4); ---> escribir hola 
write(1, "\x1b[2J", 4); ---> borrar pantalla
Para que rvnano se muestre bien en la placa, es necesario modificar ciertas zonas concretas de la pantalla sin imprimir todo 
continuamente hacia abajo (problema que pasaba al pasar directamente la versión de qemu a la placa). por ejemplo, pulsar una flecha
para mover el cursor.
Las diferencias con la versión inicial de qemu es que las secuencia ANSI no las interpretaba xv6, las interpretaba la consola de minicom en 
el ordenador. 
*/
#define ANSI_MAX_PARAMS 8

/*Estado de la máquina que interpreta las secuencias ANSI:
- normal: se reciben caracteres normales
- ESC: se acaba de recbiri el byte del caracter ESC: 0x1b
- CSI: se ha recibido el caracter ESC seguido de corcherte abierto '[', osea se leen parametros hasta el caracter def inal de la secuencia (H)*/
enum fbconsole_parser_state {
  FBCONSOLE_STATE_NORMAL,
  FBCONSOLE_STATE_ESC,
  FBCONSOLE_STATE_CSI
};

struct {
  struct spinlock lock;
  //@ virtual para acceder al framebuffer
  volatile uint32 *framebuffer;
  //posicion actual del cursos, inician en 0 ambos
  uint32 column;
  uint32 row;
  //posicion guardada con ESC[s, se 
  uint32 saved_column;
  uint32 saved_row;
  //colores de la consola. si se usa "reverse_vide" simplemente se invierte los colores del fondo y la letra 
  //util para poner consola blanca y letras negras
  uint32 foreground;
  uint32 background;
  int reverse_video;
  //estado persistente del parser ANSI
  enum fbconsole_parser_state parser_state;
  //parámetros de la secuencia ANSI CSI actual. EJ: ESC[12;40H: parameters[0] = 12 parameters[1] = 40
  int parameters[ANSI_MAX_PARAMS];
  int parameter_count;
  //la secuencia tiene '?'. se usa para secuencias privadas
  int private_sequence;
  //el programa permite que se vea el cursor
   int cursor_visible;
   //el cursor está dibujado físicamente en el framebuffer
   //son estados diferntes porque el cursor se retira temporalmente antes de modificar la pantalla
  int cursor_drawn;
  //la consola solo puede dibujar despues de que el hdmi esté inicializado
  int ready;
} fbcons;

/*
  Nunca se debe llamar a printf() desde este fichero después de
  activar fbcons.ready: printf -> consputc -> fbconsole_putc,
  porque ocurriria una recursion infinnita
 */

//devuelve el color con el que deben pintarse los caracteres (piseles del glyph)
static uint32
fbconsole_active_foreground_locked(void)
{
  return fbcons.reverse_video
    ? fbcons.background
    : fbcons.foreground;
}

//devuelve el color con el que debe pintarse el background de la celda
static uint32
fbconsole_active_background_locked(void)
{
  return fbcons.reverse_video
    ? fbcons.foreground
    : fbcons.background;
}

static void
fbconsole_put_pixel_locked(uint32 x, uint32 y, uint32 color)
{
  if(x >= HDMI_FB_WIDTH || y >= HDMI_FB_HEIGHT)
    return;

  fbcons.framebuffer[
    ((uint64)y * HDMI_FB_WIDTH) + x
  ] = color;
}

/*rellena el rectángulo del frambuffer de un color (para borrar una línea, borrar pantalla completa, borrar última línea al hacer scroll, inicializar pantalla a un fondo)

SI se sale de los límites del framebuffer, pues recorta el rectángulo*/
static void
fbconsole_fill_rect_locked(uint32 x,
                           uint32 y,
                           uint32 width,
                           uint32 height,
                           uint32 color)
{
  //El punto inicial está fuera del framebuffer

  if(x >= HDMI_FB_WIDTH || y >= HDMI_FB_HEIGHT)
    return;

  if(width == 0 || height == 0)
    return;

  //Recortar horizontalmente
  if(x + width > HDMI_FB_WIDTH)
    width = HDMI_FB_WIDTH - x;

  //Recortar verticalmente
  if(y + height > HDMI_FB_HEIGHT)
    height = HDMI_FB_HEIGHT - y;

  for(uint32 current_y = y;
      current_y < y + height;
      current_y++){

    uint64 row_base =
      (uint64)current_y * HDMI_FB_WIDTH;

    for(uint32 current_x = x;
        current_x < x + width;
        current_x++){

      fbcons.framebuffer[row_base + current_x] = color;
    }
  }

  //Ordenar las escrituras antes de limpiar la caché
  asm volatile("fence rw, rw" ::: "memory");

  //El DC8200 lee el framebuffer mediante DMA, por lo que debe recibir la versión actualizada de este rectángulo
  hdmi_cache_clean_rect(x, y, width, height);
}

static const uint8 *
fbconsole_get_glyph(int character)
{
  if(character < 32 || character > 126)
    return font8x8_basic[(uint)'?'];

  return font8x8_basic[(uint)character];
}


/*static void
fbconsole_draw_char_locked(uint32 column,
                           uint32 row,
                           int character)
{
  const uint8 *glyph;
  uint32 pixel_x;
  uint32 pixel_y;

  glyph = fbconsole_get_glyph(character);

  pixel_x = column * FBCONSOLE_CHAR_WIDTH;
  pixel_y = row * FBCONSOLE_CHAR_HEIGHT;

  for(uint32 source_row = 0;
      source_row < FONT_SOURCE_HEIGHT;
      source_row++){

    uint8 row_bits = glyph[source_row];

    for(uint32 vertical_copy = 0;
        vertical_copy < 2U;
        vertical_copy++){

      uint32 destination_y =
        pixel_y +
        (source_row * 2U) +
        vertical_copy;

      for(uint32 source_column = 0;
          source_column < FONT_SOURCE_WIDTH;
          source_column++){

        uint32 color;

      
          En esta tabla, bit 0 = píxel izquierdo.
       
        if(row_bits & (1U << source_column))
          color = FBCONSOLE_FOREGROUND;
        else
          color = FBCONSOLE_BACKGROUND;

        fbconsole_put_pixel_locked(
          pixel_x + source_column,
          destination_y,
          color
        );
      }
    }
  }

  asm volatile("fence rw, rw" ::: "memory");

  hdmi_cache_clean_rect(
    pixel_x,
    pixel_y,
    FBCONSOLE_CHAR_WIDTH,
    FBCONSOLE_CHAR_HEIGHT
  );
}*/

/*DIbuja una celda por completo (dibujar tanto el fondo como el caracter/glyph). Esto es necesario porque al sustituir un caracter por otro, se deben nquitar los pixeles
del anterior*/
static void
fbconsole_draw_char_locked(uint32 column,
                           uint32 row,
                           int character)
{
  const uint8 *glyph;
  uint32 pixel_x;
  uint32 pixel_y;
  uint32 foreground;
  uint32 background;

  //mpedir escrituras fuera de la matriz lógica de caracteres
  if(column >= FBCONSOLE_COLS || row >= FBCONSOLE_ROWS)
    return;

  glyph = fbconsole_get_glyph(character);

  pixel_x = column * FBCONSOLE_CHAR_WIDTH;
  pixel_y = row * FBCONSOLE_CHAR_HEIGHT;

  /*
    Obtener los colores activos. la imagen en inverso estarán
    intercambiados
   */
  foreground = fbconsole_active_foreground_locked();
  background = fbconsole_active_background_locked();

  for(uint32 source_row = 0;
      source_row < FONT_SOURCE_HEIGHT;
      source_row++){

    uint8 row_bits = glyph[source_row];

    /*
      La fuente original mide 8 píxeles de alto.
      Cada fila se copia dos veces para obtener 16 píxeles.
     */
    for(uint32 vertical_copy = 0;
        vertical_copy < 2U;
        vertical_copy++){

      uint32 destination_y =
        pixel_y + (source_row * 2U) + vertical_copy;

      for(uint32 source_column = 0;
          source_column < FONT_SOURCE_WIDTH;
          source_column++){

        /*
          En esta tabla, bit 0 = píxel izquierdo.
         */
        uint32 color =
          (row_bits & (1U << source_column))
            ? foreground
            : background;

        fbconsole_put_pixel_locked(
          pixel_x + source_column,
          destination_y,
          color
        );
      }
    }
  }

  asm volatile("fence rw, rw" ::: "memory");

  hdmi_cache_clean_rect(
    pixel_x,
    pixel_y,
    FBCONSOLE_CHAR_WIDTH,
    FBCONSOLE_CHAR_HEIGHT
  );
}



/*
FUncion para pintar el cursor (el parpadeo): modifica físicamente los pixeles de la celda 
XOR con 0x00ffffff transforma:
    negro   -> blanco
    blanco  -> negro
ósea que ejecutar esta función 2 veces va a restaurar al original
*/
static void
fbconsole_toggle_cursor_locked(void)
{
  uint32 pixel_x;
  uint32 pixel_y;

  if(fbcons.column >= FBCONSOLE_COLS ||
     fbcons.row >= FBCONSOLE_ROWS)
    return;

  pixel_x = fbcons.column * FBCONSOLE_CHAR_WIDTH;
  pixel_y = fbcons.row * FBCONSOLE_CHAR_HEIGHT;

  for(uint32 y = pixel_y;
      y < pixel_y + FBCONSOLE_CHAR_HEIGHT;
      y++){

    uint64 row_base =
      (uint64)y * HDMI_FB_WIDTH;

    for(uint32 x = pixel_x;
        x < pixel_x + FBCONSOLE_CHAR_WIDTH;
        x++){

      fbcons.framebuffer[row_base + x] ^= 0x00ffffffU;
    }
  }

  asm volatile("fence rw, rw" ::: "memory");

  hdmi_cache_clean_rect(
    pixel_x,
    pixel_y,
    FBCONSOLE_CHAR_WIDTH,
    FBCONSOLE_CHAR_HEIGHT
  );
}

/*Si el cursor del frambeffuer (para indicar la posición del cursor) estaba dibujado, lo retira 
antes de escribir, borrar o moverse de la posición actual. Esto se hace porque el cursos se debe quitar al dibujar o borrar o al moverlo a otra posición.
Esta función no cambia el valor de la variable "cursor_visible"*/
static void
fbconsole_cursor_hide_locked(void)
{
  if(fbcons.cursor_drawn){
    fbconsole_toggle_cursor_locked();
    fbcons.cursor_drawn = 0;
  }
}

/*
Decide si mostrar el cursos y de ser así llama a fbconsole_toggle_cursor_locked().
LO mmuestra si la aplicación no hay oculado el cursor, o si todavía no está dibuja o si no se está en mitad de una secuencia ANSI*/
static void
fbconsole_cursor_show_locked(void)
{
  if(fbcons.cursor_visible &&
     !fbcons.cursor_drawn &&
     fbcons.parser_state == FBCONSOLE_STATE_NORMAL){

    fbconsole_toggle_cursor_locked();
    fbcons.cursor_drawn = 1;
  }
}

static void
fbconsole_clear_last_text_row_locked(void)
{
  uint32 first_y;

  first_y =
    (FBCONSOLE_ROWS - 1U) *
    FBCONSOLE_CHAR_HEIGHT;

  for(uint32 y = first_y;
      y < HDMI_FB_HEIGHT;
      y++){

    uint64 row_base =
      (uint64)y * HDMI_FB_WIDTH;

    for(uint32 x = 0;
        x < HDMI_FB_WIDTH;
        x++){

      fbcons.framebuffer[row_base + x] =
        FBCONSOLE_BACKGROUND;
    }
  }
}

static void
fbconsole_scroll_locked(void)
{
  uint64 source_pixel_offset;
  uint64 move_bytes;

  /*
    Desplaza la pantalla 16 píxeles hacia arriba.
   */
  source_pixel_offset =
    (uint64)FBCONSOLE_CHAR_HEIGHT *
    (uint64)HDMI_FB_WIDTH;

  move_bytes =
    ((uint64)HDMI_FB_HEIGHT -
     (uint64)FBCONSOLE_CHAR_HEIGHT) *
    (uint64)HDMI_FB_STRIDE;

  memmove(
    (void *)(uint64)fbcons.framebuffer,
    (void *)(uint64)(fbcons.framebuffer +
                     source_pixel_offset),
    (uint)move_bytes
  );

  fbconsole_clear_last_text_row_locked();

  asm volatile("fence rw, rw" ::: "memory");

  /*
    El scroll modifica prácticamente toda la pantalla.
   */
  hdmi_cache_clean_full();

  fbcons.row = FBCONSOLE_ROWS - 1U;
  fbcons.column = 0;
}

static void
fbconsole_newline_locked(void)
{
  fbcons.column = 0;
  fbcons.row++;

  if(fbcons.row >= FBCONSOLE_ROWS)
    fbconsole_scroll_locked();
}

static void
fbconsole_backspace_locked(void)
{
  if(fbcons.column > 0){
    fbcons.column--;
  } else if(fbcons.row > 0){
    fbcons.row--;
    fbcons.column = FBCONSOLE_COLS - 1U;
  } else {
    return;
  }

  fbconsole_draw_char_locked(
    fbcons.column,
    fbcons.row,
    ' '
  );
}

static void
fbconsole_printable_locked(int character)
{
  if(fbcons.column >= FBCONSOLE_COLS)
    fbconsole_newline_locked();

  fbconsole_draw_char_locked(
    fbcons.column,
    fbcons.row,
    character
  );

  fbcons.column++;

  if(fbcons.column >= FBCONSOLE_COLS)
    fbconsole_newline_locked();
}

static void
fbconsole_tab_locked(void)
{
  uint32 spaces;

  spaces =
    FBCONSOLE_TAB_WIDTH -
    (fbcons.column % FBCONSOLE_TAB_WIDTH);

  for(uint32 i = 0; i < spaces; i++)
    fbconsole_printable_locked(' ');
}

void
fbconsole_init(void)
{
  initlock(&fbcons.lock, "fbconsole");

  fbcons.framebuffer = 0;
  fbcons.column = 0;
  fbcons.row = 0;
  fbcons.ready = 0;

  if(!hdmi_is_ready())
    return;

  fbcons.framebuffer = hdmi_framebuffer_ptr();

  if(fbcons.framebuffer == 0)
    return;

  __sync_synchronize();
  fbcons.ready = 1;
  __sync_synchronize();
}

int
fbconsole_is_ready(void)
{
  return fbcons.ready != 0;
}

void
fbconsole_clear(void)
{
  uint64 pixel_count;

  if(!fbcons.ready)
    return;

  acquire(&fbcons.lock);

  pixel_count =
    (uint64)HDMI_FB_WIDTH *
    (uint64)HDMI_FB_HEIGHT;

  for(uint64 i = 0;
      i < pixel_count;
      i++){

    fbcons.framebuffer[i] =
      FBCONSOLE_BACKGROUND;
  }

  fbcons.column = 0;
  fbcons.row = 0;

  asm volatile("fence rw, rw" ::: "memory");
  hdmi_cache_clean_full();

  release(&fbcons.lock);
}

void
fbconsole_putc(int character)
{
  if(!fbcons.ready)
    return;

  acquire(&fbcons.lock);

  switch(character){
  case '\n':
    fbconsole_newline_locked();
    break;

  case '\r':
    fbcons.column = 0;
    break;

  case '\b':
    fbconsole_backspace_locked();
    break;

  case '\t':
    fbconsole_tab_locked();
    break;

  default:
    if(character >= 32 && character <= 126)
      fbconsole_printable_locked(character);
    break;
  }

  release(&fbcons.lock);
}
