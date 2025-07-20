// pwd.c - imprime el directorio actual
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
pwd(void) {
  struct stat st, pst;
  //char buf[512];
  char name[DIRSIZ+1];
  int fd, pfd;
  struct dirent de;
  int inum;

  if ((fd = open(".", 0)) < 0) {
    fprintf(2, "pwd: cannot open .\n");
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "pwd: cannot stat .\n");
    close(fd);
    return;
  }

  inum = st.ino;

  if ((pfd = open("..", 0)) < 0) {
    fprintf(2, "pwd: cannot open ..\n");
    close(fd);
    return;
  }

  if (fstat(pfd, &pst) < 0) {
    fprintf(2, "pwd: cannot stat ..\n");
    close(fd);
    close(pfd);
    return;
  }

  if (inum == pst.ino) {
    // estamos en raíz
    printf("/");
    close(fd);
    close(pfd);
    return;
  }

  while (read(pfd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == inum) {
      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = '\0';
      break;
    }
  }

  close(fd);
  close(pfd);

  chdir("..");
  pwd();
  printf("/%s", name);
}

int
main(int argc, char *argv[])
{
  pwd();
  printf("\n");
  exit(0);
}
