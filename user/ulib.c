#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

//
// wrapper so that it's OK if main() does not call exit().
//
void
start()
{
  extern int main();
  main();
  exit(0);
}

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

//he mejorado atoi para aceptar valores negativos
int
atoi(const char *s)
{
  int n; //acumulador del numero final
  int sign = 1; //el signo por defecto es positivo
  n = 0;

  //vamos a detectar el signo del numero
  if (*s == '-') { //si el signo es negativo lo guardamos
    sign = -1;
    s++;    // avanza al siguiente carácter
  } else if (*s == '+') {
    s++;   // solo avanza al siguiente caracter si el signo es positivo
  }
  //convierte cada caracter numerico a su valor entero
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  
  //multiplicamos el signo por el numero para aplicarle el signo
  return sign * n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while(n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}


//nuevas funciones

/*Le pasamos un string y determina si cada caracter 
es un digito del 0 al 9, retorna 0 si la cadena (al menos 1 caracter) no es un numero, 
o 1 si la cadena es un numero*/
// En user/ulib.c
int is_number(char *s) {
  if (*s == '-' || *s == '+') s++; // saltamos signo si lo hay y movemos el puntero al siguiente
  if (*s == 0) return 0; //si solo tenemos un signo o la cadena es vacia, pues no es un numero
  
  //vamos a recorrer toda la cadena, para comprobar si cada caracter es un digito (hasta llegar al caracter nulo)
  while (*s) {
    //vamos a comparar si el caracter actual es un digito del 1 al 9, las letras ascii tienen un 
    //num diferente asignado asi que es por eso que los podemos diferenciar
    if (*s < '0' || *s > '9')
      //si se entra aqui, entonces no es un numero
      return 0;
    s++;
  }

  //si se llega hasta aqui, la cadena es un numero
  return 1;
}

