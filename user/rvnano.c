#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define CTRL_KEY(k) ((k) & 0x1f)

#define SCREEN_ROWS 24
#define SCREEN_COLS 80

#define TEXT_TOP    2
#define TEXT_BOTTOM 21

#define MAX_LINES       128
#define MAX_LINE_LENGTH 128
#define MAX_FILENAME 64

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

  char filename[MAX_FILENAME];
  char status[80];
};

static struct editor_state editor;

//variable global para saber si hay cambios
static int quit_pending;
/*
 * Portapapeles interno del editor.
 * Ctrl+K corta una línea y Ctrl+U la pega.
 */
static char clipboard[MAX_LINE_LENGTH];
static int clipboard_length;

/*
 * Prototipos de funciones
 */
static void editor_set_status(char *message);
static int editor_load_file(char *filename);
static int editor_save_file(void);
static void editor_cut_line(void);
static void editor_paste_line(void);

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

static void editor_init(char *filename)
{
  memset(&editor, 0, sizeof(editor));

  //inicializar variables del editor 
  quit_pending = 0;
  clipboard_length = 0;
  clipboard[0] = '\0';

  editor.line_count = 1;
  editor.lines[0].length = 0;
  editor.lines[0].text[0] = '\0';

  editor.cursor_x = 0;
  editor.cursor_y = 0;

  editor.row_offset = 0;
  editor.column_offset = 0;

  editor.modified = 0;

  if(filename != 0){
    int length = strlen(filename);

    if(length >= MAX_FILENAME)
      length = MAX_FILENAME - 1;

    memmove(editor.filename, filename, length);
    editor.filename[length] = '\0';
  } else {
    editor.filename[0] = '\0';
  }

  editor.status[0] = '\0';
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

//imprime el estado del documento
static void editor_draw_status(void)
{
  terminal_write("\x1b[2K");

  if(editor.status[0] != '\0')
    terminal_write(editor.status);

  terminal_write("\r\n");
}

static void
editor_draw_title(void)
{
  terminal_write("\x1b[7m");
  terminal_write("\x1b[2K");

  terminal_write(" rvnano - ");

  if(editor.filename[0] != '\0')
    terminal_write(editor.filename);
  else
    terminal_write("[sin nombre]");

  if(editor.modified)
    terminal_write("  [modificado]");

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
  terminal_write("^O Guardar  ^X Salir  ^K Cortar  ^U Pegar  Flechas Mover");
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
  editor_draw_status();
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

/*
 * Corta la línea actual y la guarda en el portapapeles.
 */
static void
editor_cut_line(void)
{
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  /*
   * Guardar la línea actual en el portapapeles.
   */
  clipboard_length = line->length;

  if(clipboard_length > 0){
    memmove(
      clipboard,
      line->text,
      clipboard_length
    );
  }

  clipboard[clipboard_length] = '\0';

  /*
   * Si solamente existe una línea, se vacía,
   * pero se mantiene el documento con una línea.
   */
  if(editor.line_count == 1){
    line->length = 0;
    line->text[0] = '\0';

    editor.cursor_x = 0;
    editor.modified = 1;

    editor_set_status("Linea cortada");
    return;
  }

  /*
   * Desplazar hacia arriba las líneas posteriores.
   */
  for(int i = editor.cursor_y;
      i < editor.line_count - 1;
      i++){
    editor.lines[i] = editor.lines[i + 1];
  }

  editor.line_count--;

  /*
   * Si se eliminó la última línea, mover el cursor
   * a la nueva última línea.
   */
  if(editor.cursor_y >= editor.line_count)
    editor.cursor_y = editor.line_count - 1;

  editor.cursor_x = 0;
  editor.modified = 1;

  editor_set_status("Linea cortada");
}

/*
 * Pega el contenido del portapapeles como una línea nueva
 * antes de la línea actual.
 */
static void
editor_paste_line(void)
{
  if(clipboard_length == 0){
    editor_set_status("Portapapeles vacio");
    return;
  }

  if(editor.line_count >= MAX_LINES){
    editor_set_status("No se pueden crear mas lineas");
    return;
  }

  /*
   * Desplazar hacia abajo la línea actual y las posteriores.
   */
  for(int i = editor.line_count;
      i > editor.cursor_y;
      i--){
    editor.lines[i] = editor.lines[i - 1];
  }

  /*
   * Copiar el portapapeles a la nueva línea.
   */
  struct editor_line *line =
    &editor.lines[editor.cursor_y];

  memmove(
    line->text,
    clipboard,
    clipboard_length
  );

  line->length = clipboard_length;
  line->text[line->length] = '\0';

  editor.line_count++;

  /*
   * Colocar el cursor al final de la línea pegada.
   */
  editor.cursor_x = line->length;

  editor.modified = 1;
  editor_set_status("Linea pegada");
}

static void editor_process_key(void)
{
  int key = editor_read_key();

  if(key < 0)
    return;

  /*
   * Si había una salida pendiente y el usuario pulsa otra tecla,
   * se cancela la confirmación de salida.
   */
  if(key != CTRL_KEY('x'))
    quit_pending = 0;

  switch(key){

  /*
   * Ctrl+O: guardar archivo.
   */
  case CTRL_KEY('o'):
    if(editor_save_file() == 0)
      quit_pending = 0;
    break;

  /*
   * Ctrl+X: salir.
   *
   * Si hay cambios sin guardar, obliga a pulsar Ctrl+X dos veces.
   */
  case CTRL_KEY('x'):
    if(editor.modified && !quit_pending){
      quit_pending = 1;

      editor_set_status(
        "Cambios sin guardar: Ctrl+O guarda, Ctrl+X otra vez descarta"
      );

      break;
    }

    editor_cleanup();
    exit(0);
    break;

  /*
   * Ctrl+K: cortar la línea actual.
   */
  case CTRL_KEY('k'):
    editor_cut_line();
    break;

  /*
   * Ctrl+U: pegar la línea previamente cortada.
   */
  case CTRL_KEY('u'):
    editor_paste_line();
    break;

  /*
   * Movimiento del cursor.
   */
  case KEY_ARROW_UP:
  case KEY_ARROW_DOWN:
  case KEY_ARROW_LEFT:
  case KEY_ARROW_RIGHT:
  case KEY_HOME:
  case KEY_END:
    editor_move_cursor(key);
    break;

  /*
   * Tecla Delete.
   */
  case KEY_DELETE:
    editor_delete_character();
    break;

  /*
   * Backspace puede llegar como 127 o Ctrl+H.
   */
  case 127:
  case CTRL_KEY('h'):
    editor_backspace();
    break;

  /*
   * Enter.
   */
  case '\r':
  case '\n':
    editor_insert_newline();
    break;

  /*
   * Escape, de momento no hace nada.
   */
  case '\x1b':
    break;

  /*
   * Caracteres imprimibles normales.
   */
  default:
    if(key >= 32 && key <= 126)
      editor_insert_character(key);
    break;
  }
}

//función para mostrar mensajes del estado del fichero 
static void
editor_set_status(char *message)
{
  int length = strlen(message);

  if(length >= sizeof(editor.status))
    length = sizeof(editor.status) - 1;

  memmove(editor.status, message, length);
  editor.status[length] = '\0';
}

//funcion para cargar un fichero existente 
static int
editor_load_file(char *filename)
{
  int fd;
  char buffer[256];
  int n;

  fd = open(filename, O_RDONLY);

  /*
   * Si no existe, no es un error:
   * se comienza con un archivo vacío.
   */
  if(fd < 0){
    editor.line_count = 1;
    editor.lines[0].length = 0;
    editor.lines[0].text[0] = '\0';

    editor_set_status("Archivo nuevo");
    return 0;
  }

  editor.line_count = 1;
  editor.lines[0].length = 0;
  editor.lines[0].text[0] = '\0';

  while((n = read(fd, buffer, sizeof(buffer))) > 0){
    for(int i = 0; i < n; i++){
      unsigned char c = buffer[i];

      if(c == '\r')
        continue;

      if(c == '\n'){
        if(editor.line_count >= MAX_LINES){
          close(fd);
          editor_set_status("Archivo demasiado largo");
          return -1;
        }

        editor.line_count++;
        editor.lines[editor.line_count - 1].length = 0;
        editor.lines[editor.line_count - 1].text[0] = '\0';
        continue;
      }

      struct editor_line *line =
        &editor.lines[editor.line_count - 1];

      if(line->length >= MAX_LINE_LENGTH - 1){
        close(fd);
        editor_set_status("Una linea es demasiado larga");
        return -1;
      }

      line->text[line->length++] = c;
      line->text[line->length] = '\0';
    }
  }

  close(fd);

  /*
   * Si el archivo termina en '\n', el código anterior crea una última
   * línea vacía. Eso es correcto para un editor de texto.
   */
  editor.modified = 0;
  editor_set_status("Archivo cargado");

  return 0;
}

//guardar el fichero 
static int
editor_save_file(void)
{
  int fd;

  if(editor.filename[0] == '\0'){
    editor_set_status("No hay nombre de archivo");
    return -1;
  }

  /*
   * xv6 no suele tener O_TRUNC.
   * Eliminamos el archivo y lo volvemos a crear.
   */
  unlink(editor.filename);

  fd = open(editor.filename, O_CREATE | O_WRONLY);

  if(fd < 0){
    editor_set_status("Error creando el archivo");
    return -1;
  }

  for(int i = 0; i < editor.line_count; i++){
    struct editor_line *line = &editor.lines[i];

    int written = 0;

    while(written < line->length){
      int result = write(
        fd,
        line->text + written,
        line->length - written
      );

      if(result <= 0){
        close(fd);
        editor_set_status("Error escribiendo el archivo");
        return -1;
      }

      written += result;
    }

    /*
     * Escribimos salto entre líneas.
     *
     * También dejamos newline en la última línea, algo habitual
     * para fuentes ensamblador.
     */
    if(write(fd, "\n", 1) != 1){
      close(fd);
      editor_set_status("Error escribiendo salto de linea");
      return -1;
    }
  }

  close(fd);

  editor.modified = 0;
  editor_set_status("Archivo guardado");

  return 0;
}

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Uso: rvnano archivo\n");
    exit(1);
  }

  editor_init(argv[1]);
  editor_load_file(argv[1]);

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