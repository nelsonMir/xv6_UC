#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
restore_console(void)
{
  term_cooked();

  // Restaurar atributos ANSI y mostrar cursor.
  write(1, "\x1b[0m", 4);
  write(1, "\x1b[?25h", 6);

  printf("\nConsola restaurada.\n");
}

int
main(int argc, char *argv[])
{
  char c;

  printf("Prueba de consola raw\n");
  printf("Pulsa teclas y se mostrara su valor hexadecimal.\n");
  printf("Pulsa Ctrl+Q para terminar.\n\n");

  if(term_raw() < 0){
    fprintf(2, "rawtest: no se pudo activar el modo raw\n");
    exit(1);
  }

  while(1){
    int result = read(0, &c, 1);

    if(result != 1){
      restore_console();
      fprintf(2, "rawtest: error leyendo la consola\n");
      exit(1);
    }

    unsigned char value = (unsigned char)c;

    /*
     * Ctrl+Q = 17 = 0x11.
     */
    if(value == 17)
      break;

    //detector de flechas, como una fleccha esta representada por varios caracteres ascii
    // ejemplo ESC + [ + A, para no imprimir las lineas separadas las agrupo 
    if(value == 0x1b){
      char seq[2];

      if(read(0, &seq[0], 1) != 1)
        continue;

      if(read(0, &seq[1], 1) != 1)
        continue;

      if(seq[0] == '['){
        switch(seq[1]){
        case 'A':
          printf("Flecha arriba\n");
          break;

        case 'B':
          printf("Flecha abajo\n");
          break;

        case 'C':
          printf("Flecha derecha\n");
          break;

        case 'D':
          printf("Flecha izquierda\n");
          break;

        default:
          printf("Secuencia ESC desconocida\n");
          break;
        }
      }

      continue;
    }
    
    printf("byte: 0x%x", value);

    if(value >= 32 && value <= 126){
      printf("  caracter: '");
      write(1, &c, 1);
      printf("'");
    }
    printf("\n");
  }

  restore_console();
  exit(0);
}