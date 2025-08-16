// init(1): crea/abre /console (mknod si falta) y lanza sh.\r
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static void
open_console(void)
{
  // intenta abrir; si falla, crea el nodo de dispositivo y reintenta\r
  int fd = open("console", O_RDWR);
  if(fd < 0){
    // mayor=1 (CONSOLE), menor=1 como en xv6\r
    mknod("console", 1, 1);
    fd = open("console", O_RDWR);
  }
  if(fd >= 0){
    // duplica a stdin/stdout/stderr\r
    if(fd != 0) dup(fd);
    if(fd != 1) dup(fd);
    if(fd != 2) dup(fd);
  }
}

int
main(void)
{
  open_console();
  printf("init: starting sh\r\n");

  for(;;){
    int pid = fork();
    if(pid < 0){
      printf("init: fork failed\r\n");
      exit(1);
    }
    if(pid == 0){
      // hijo: ejecuta la shell\r
      char *argv[] = { "sh", 0 };
      exec("sh", argv);
      printf("init: exec sh failed\r\n");
      exit(1);
    }
    // padre: espera shell\r
    int xstatus = 0;
    wait(&xstatus);
    printf("init: sh exited (status=%d)\r\n", xstatus);
  }
}
