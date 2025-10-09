// user/init.c — proceso init de xv6 adaptado
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

#define DEV_CONSOLE 1  // major del dispositivo consola en xv6

static void
open_console(void)
{
  int fd;

  unlink("console");
  if(mknod("console", DEV_CONSOLE, 0) < 0){
    printf("init: mknod console failed\r\n");
  }

  fd = open("console", O_RDWR);
  if(fd < 0){
    printf("init: cannot open console\r\n");
    exit(1);
  }

  // dup stdout/stderr
  dup(fd);
  dup(fd);
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
      char *argv[] = { "sh", 0 };
      exec("sh", argv);
      printf("init: exec sh failed\r\n");
      exit(1);
    }
    int xstatus = 0;
    wait(&xstatus);
    printf("init: sh exited (status=%d)\r\n", xstatus);
  }
}
