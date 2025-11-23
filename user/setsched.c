#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "uso: setsched <politica>\n");
    fprintf(2, "  politica 0=RR, 1=FCFS, 2=priority\n");
    exit(1);
  }

  int p = atoi(argv[1]);

  if (setscheduler(p) < 0) {
    fprintf(2, "setsched: politica invalida %d\n", p);
    exit(1);
  }

  exit(0);
}
