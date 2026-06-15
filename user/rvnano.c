#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define CTRL_KEY(k) ((k) & 0x1f)

#define SCREEN_ROWS 24
#define SCREEN_COLS 80

#define TEXT_TOP    2
#define TEXT_BOTTOM 22

#define MAX_LINES       128
#define MAX_LINE_LENGTH 128

enum editor_key {
  KEY_ARROW_UP = 1000,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_DELETE,
  KEY_HOME,
  KEY_END
};

struct editor_line {
  char text[MAX_LINE_LENGTH];
  int length;
};

struct editor_state {
  struct editor_line lines[MAX_LINES];

  int line_count;

  int cursor_x;
  int cursor_y;

  int row_offset;
  int column_offset;

  int modified;
};

static struct editor_state editor;

/*
 * Escribe una cadena completa sin depender de printf.
 */
static void
terminal_write(char *text)
{
  write(1, text, strlen(text));
}

/*
 * Restaura la consola antes de regresar a la shell.
 */
static void
editor_cleanup(void)
{
  term_cooked();

  terminal_write("\x1b[0m");    // Restaurar atributos.
  terminal_write("\x1b[?25h");  // Mostrar cursor.
  terminal_write("\x1b[2J");    // Limpiar pantalla.
  terminal_write("\x1b[H");     // Cursor arriba.
}

/*
 * Lee una tecla o una secuencia especial.
 */
static int
editor_read_key(void)
{
  char c;

  if(read(0, &c, 1) != 1)
    return -1;

  if(c != '\x1b')
    return (unsigned char)c;

  char sequence[3];

  if(read(0, &sequence[0], 1) != 1)
    return '\x1b';

  if(read(0, &sequence[1], 1) != 1)
    return '\x1b';

  if(sequence[0] == '['){
    if(sequence[1] >= '0' && sequence[1] <= '9'){
      if(read(0, &sequence[2], 1) != 1)
        return '\x1b';

      if(sequence[2] == '~'){
        switch(sequence[1]){
        case '1':
        case '7':
          return KEY_HOME;

        case '3':
          return KEY_DELETE;

        case '4':
        case '8':
          return KEY_END;
        }
      }
    } else {
      switch(sequence[1]){
      case 'A':
        return KEY_ARROW_UP;

      case 'B':
        return KEY_ARROW_DOWN;

      case 'C':
        return KEY_ARROW_RIGHT;

      case 'D':
        return KEY_ARROW_LEFT;

      case 'H':
        return KEY_HOME;

      case 'F':
        return KEY_END;
      }
    }
  }

  return '\x1b';
}

static void
editor_init(void)
{
  memset(&editor, 0, sizeof(editor));

  editor.line_count = 1;
  editor.lines[0].length = 0;
  editor.lines[0].text[0] = '\0';

  editor.cursor_x = 0;
  editor.cursor_y = 0;

  editor.row_offset = 0;
  editor.column_offset = 0;

  editor.modified = 0;
}

/*
 * Mantiene el cursor dentro de la ventana visible.
 */
static void
editor_scroll(void)
{
  int visible_rows = TEXT_BOTTOM - TEXT_TOP + 1;
  int visible_columns = SCREEN_COLS;

  if(editor.cursor_y < editor.row_offset)
    editor.row_offset = editor.cursor_y;

  if(editor.cursor_y >= editor.row_offset + visible_rows)
    editor.row_offset =
      editor.cursor_y - visible_rows + 1;

  if(editor.cursor_x < editor.column_offset)
    editor.column_offset = editor.cursor_x;

  if(editor.cursor_x >= editor.column_offset + visible_columns)
    editor.column_offset =
      editor.cursor_x - visible_columns + 1;
}

static void
editor_draw_title(void)
{
  terminal_write("\x1b[7m");
  terminal_write("\x1b[2K");

  printf(" rvnano - editor xv6");

  if(editor.modified)
    printf("  [modificado]");

  terminal_write("\x1b[0m");
  terminal_write("\r\n");
}

static void
editor_draw_rows(void)
{
  int visible_rows = TEXT_BOTTOM - TEXT_TOP + 1;

  for(int screen_row = 0;
      screen_row < visible_rows;
      screen_row++){

    int file_row = editor.row_offset + screen_row;

    terminal_write("\x1b[2K");

    if(file_row >= editor.line_count){
      terminal_write("~");
    } else {
      struct editor_line *line = &editor.lines[file_row];

      int start = editor.column_offset;
      int available = line->length - start;

      if(available > 0){
        if(available > SCREEN_COLS)
          available = SCREEN_COLS;

        write(1, line->text + start, available);
      }
    }

    terminal_write("\r\n");
  }
}

static void
editor_draw_help(void)
{
  terminal_write("\x1b[7m");
  terminal_write("\x1b[2K");
  terminal_write("^X Salir   Flechas Mover   Enter Nueva linea   Backspace Borrar");
  terminal_write("\x1b[0m");
}

/*
 * Redibuja toda la pantalla.
 */
static void
editor_refresh_screen(void)
{
  editor_scroll();

  terminal_write("\x1b[?25l");
  terminal_write("\x1b[H");

  editor_draw_title();
  editor_draw_rows();
  editor_draw_help();

  /*
   * ANSI usa filas y columnas empezando en 1.
   *
   * La primera línea de texto está en la fila 2.
   */
  int screen_y =
    editor.cursor_y - editor.row_offset + TEXT_TOP;

  int screen_x =
    editor.cursor_x - editor.column_offset + 1;

  printf("\x1b[%d;%dH", screen_y, screen_x);

  terminal_write("\x1b[?25h");
}

static void
editor_move_cursor(int key)
{
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  switch(key){
  case KEY_ARROW_LEFT:
    if(editor.cursor_x > 0){
      editor.cursor_x--;
    } else if(editor.cursor_y > 0){
      editor.cursor_y--;
      editor.cursor_x =
        editor.lines[editor.cursor_y].length;
    }
    break;

  case KEY_ARROW_RIGHT:
    if(editor.cursor_x < line->length){
      editor.cursor_x++;
    } else if(editor.cursor_y + 1 < editor.line_count){
      editor.cursor_y++;
      editor.cursor_x = 0;
    }
    break;

  case KEY_ARROW_UP:
    if(editor.cursor_y > 0)
      editor.cursor_y--;
    break;

  case KEY_ARROW_DOWN:
    if(editor.cursor_y + 1 < editor.line_count)
      editor.cursor_y++;
    break;

  case KEY_HOME:
    editor.cursor_x = 0;
    break;

  case KEY_END:
    editor.cursor_x =
      editor.lines[editor.cursor_y].length;
    break;
  }

  line = &editor.lines[editor.cursor_y];

  if(editor.cursor_x > line->length)
    editor.cursor_x = line->length;
}

static void
editor_insert_character(int character)
{
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  if(line->length >= MAX_LINE_LENGTH - 1)
    return;

  memmove(
    line->text + editor.cursor_x + 1,
    line->text + editor.cursor_x,
    line->length - editor.cursor_x
  );

  line->text[editor.cursor_x] = character;
  line->length++;
  line->text[line->length] = '\0';

  editor.cursor_x++;
  editor.modified = 1;
}

static void
editor_insert_newline(void)
{
  if(editor.line_count >= MAX_LINES)
    return;

  struct editor_line *current =
    &editor.lines[editor.cursor_y];

  /*
   * Desplazar las líneas posteriores.
   */
  for(int i = editor.line_count;
      i > editor.cursor_y + 1;
      i--){
    editor.lines[i] = editor.lines[i - 1];
  }

  struct editor_line *next =
    &editor.lines[editor.cursor_y + 1];

  int remaining =
    current->length - editor.cursor_x;

  memmove(
    next->text,
    current->text + editor.cursor_x,
    remaining
  );

  next->length = remaining;
  next->text[remaining] = '\0';

  current->length = editor.cursor_x;
  current->text[current->length] = '\0';

  editor.line_count++;
  editor.cursor_y++;
  editor.cursor_x = 0;

  editor.modified = 1;
}

static void
editor_backspace(void)
{
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  if(editor.cursor_x > 0){
    memmove(
      line->text + editor.cursor_x - 1,
      line->text + editor.cursor_x,
      line->length - editor.cursor_x
    );

    line->length--;
    line->text[line->length] = '\0';

    editor.cursor_x--;
    editor.modified = 1;
    return;
  }

  /*
   * Al principio de una línea, unirla con la anterior.
   */
  if(editor.cursor_y > 0){
    struct editor_line *previous =
      &editor.lines[editor.cursor_y - 1];

    if(previous->length + line->length
       >= MAX_LINE_LENGTH){
      return;
    }

    int previous_length = previous->length;

    memmove(
      previous->text + previous->length,
      line->text,
      line->length
    );

    previous->length += line->length;
    previous->text[previous->length] = '\0';

    for(int i = editor.cursor_y;
        i < editor.line_count - 1;
        i++){
      editor.lines[i] = editor.lines[i + 1];
    }

    editor.line_count--;
    editor.cursor_y--;
    editor.cursor_x = previous_length;

    editor.modified = 1;
  }
}

static void
editor_delete_character(void)
{
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  if(editor.cursor_x < line->length){
    memmove(
      line->text + editor.cursor_x,
      line->text + editor.cursor_x + 1,
      line->length - editor.cursor_x - 1
    );

    line->length--;
    line->text[line->length] = '\0';

    editor.modified = 1;
    return;
  }

  /*
   * Delete al final de la línea: unir con la siguiente.
   */
  if(editor.cursor_y + 1 < editor.line_count){
    struct editor_line *next =
      &editor.lines[editor.cursor_y + 1];

    if(line->length + next->length
       >= MAX_LINE_LENGTH){
      return;
    }

    memmove(
      line->text + line->length,
      next->text,
      next->length
    );

    line->length += next->length;
    line->text[line->length] = '\0';

    for(int i = editor.cursor_y + 1;
        i < editor.line_count - 1;
        i++){
      editor.lines[i] = editor.lines[i + 1];
    }

    editor.line_count--;
    editor.modified = 1;
  }
}

static void
editor_process_key(void)
{
  int key = editor_read_key();

  if(key < 0)
    return;

  switch(key){
  case CTRL_KEY('x'):
    editor_cleanup();
    exit(0);
    break;

  case KEY_ARROW_UP:
  case KEY_ARROW_DOWN:
  case KEY_ARROW_LEFT:
  case KEY_ARROW_RIGHT:
  case KEY_HOME:
  case KEY_END:
    editor_move_cursor(key);
    break;

  case KEY_DELETE:
    editor_delete_character();
    break;

  case 127:
  case CTRL_KEY('h'):
    editor_backspace();
    break;

  case '\r':
  case '\n':
    editor_insert_newline();
    break;

  default:
    if(key >= 32 && key <= 126)
      editor_insert_character(key);
    break;
  }
}

int
main(int argc, char *argv[])
{
  editor_init();

  if(term_raw() < 0){
    fprintf(2, "rvnano: no se pudo activar modo raw\n");
    exit(1);
  }

  terminal_write("\x1b[2J");
  terminal_write("\x1b[H");

  while(1){
    editor_refresh_screen();
    editor_process_key();
  }

  /*
   * No debería alcanzarse, pero mantengo restauración defensiva.
   */
  editor_cleanup();
  exit(0);
}