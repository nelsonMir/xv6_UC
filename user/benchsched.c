// user/benchsched.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// onfiguración por defecto (en ticks) 
// 1 tick son 10 ms en xv6.
#define DEF_LONG_TICKS   400   // 400 ms
#define DEF_SHORT_TICKS   80   // 800 ms
#define MAX_PROCS         128  // límite práctico de procesos

// Mensajes por pipe para medir tiempos (binario simple)
enum { MSG_STARTED = 1, MSG_DONE = 2 }; //se define un pequenio protocolo por pipes para indicar cuando un proceso haya terminado

struct msg {
  int pid;
  int type; //STARTED o DONE
  uint t;   // tick en el momento de enviar
  int role; //0 es un proceso corto, 1 es un proceso largo
};

// Trabajo CPU-bound durante target_ticks_from_now desde ahora
static void
work_cpu(uint target_ticks_from_now)
{
  uint t0 = uptime(); //uptime devuelve el número de ticks del sistema en ese momento
  volatile uint64 s = 0;
  while (uptime() - t0 < target_ticks_from_now) { //mientras no hayan pasado el numero de ticks objetivo se quema cpu sin bloquearse, para 
    //que sea el planificador el que decida cuando entregar la cpu (si hay yield o no)
    for (int i = 0; i < 100000; i++)
      s += i;
  }
}

// Enviar mensaje por pipe (fd) con timestamp actual
static void
send_msg(int fd, int type, int role)
{
  struct msg m;
  m.pid = getpid();
  m.type = type;
  m.t = uptime();
  m.role = role;
  write(fd, &m, sizeof(m));
}

static void
usage(void)
{
  fprintf(2,
    "usage: benchsched <nshort> [long_ms] [short_ms] [long_pos]\n"
    "  nshort   : numero de procesos cortos (CPU-bound)\n"
    "  long_ms  : duracion del proceso largo en ms (defecto 4000)\n"
    "  short_ms : duracion de cada corto en ms (defecto 800)\n"
    "  long_pos : 0 = largo primero, 1 = largo ultimo (defecto 0)\n"
    "\n"
    "Notas:\n"
    "  - Siempre se crea 1 proceso largo + nshort cortos.\n"
    "  - 1 tick ~ 10 ms en xv6.\n"
    "Ejemplos:\n"
    "  benchsched 5              # 1 largo + 5 cortos, largo primero\n"
    "  benchsched 5 6000 500 1   # largo ~6s, cortos ~0.5s, largo al final\n");
}

struct rec { //el array de registros del padre
  int pid;
  int role;         // 0=CPU corto, 1=CPU largo
  uint t_create;    // tiempo de inicio del experimento 
  uint t_start;     // primer tick en que el hijo se ejecuta
  uint t_finish;    // tick cuando el hijo termina
  int started_seen;
  int done_seen;
}; //el padre va leyendo del pipe y en base a eso rellena t_start y t_finish de sus hijos

static int
find_idx_by_pid(struct rec *R, int n, int pid)
{
  for (int i = 0; i < n; i++)
    if (R[i].pid == pid)
      return i;
  return -1;
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    usage();
    exit(1);
  }

  int nshort = atoi(argv[1]);
  if (nshort < 0) {
    fprintf(2, "benchsched: nshort debe ser >= 0\n");
    exit(1);
  }

  // total = 1 largo + nshort cortos
  int total = nshort + 1;
  if (total > MAX_PROCS) {
    fprintf(2, "benchsched: total procesos (1 + nshort) no puede exceder %d\n", MAX_PROCS);
    exit(1);
  }

  // Parse tiempos opcionales (ms -> ticks)
  uint long_ticks = DEF_LONG_TICKS;
  uint short_ticks = DEF_SHORT_TICKS;
  int long_pos = 0; // 0 = largo primero, 1 = largo ultimo

  if (argc >= 3) {
    int long_ms = atoi(argv[2]);
    if (long_ms > 0)
      long_ticks = long_ms / 10;
    if (long_ticks == 0)
      long_ticks = 1;
  }
  if (argc >= 4) {
    int short_ms = atoi(argv[3]);
    if (short_ms > 0)
      short_ticks = short_ms / 10;
    if (short_ticks == 0)
      short_ticks = 1;
  }
  if (argc >= 5) {
    long_pos = atoi(argv[4]) != 0; // cualquier valor no cero => 1
  }

  // Pipe para recoger eventos de todos los hijos
  int pfd[2];
  if (pipe(pfd) < 0) {
    fprintf(2, "benchsched: pipe failed\n");
    exit(1);
  }
  int rfd = pfd[0];
  int wfd = pfd[1];

  struct rec *R;

// Comprobación de límites lógicos (por si acaso)
if (total > MAX_PROCS) {
  fprintf(2, "benchsched: total procesos (1 + nshort) no puede exceder %d\n", MAX_PROCS);
  exit(1);
}

// Reservar en el heap, no en la pila
R = malloc(sizeof(struct rec) * total);
if (R == 0) {
  fprintf(2, "benchsched: malloc failed\n");
  exit(1);
}

for (int i = 0; i < total; i++) {
  R[i].pid = -1;
  R[i].role = 0;
  R[i].t_create = 0;
  R[i].t_start = 0;
  R[i].t_finish = 0;
  R[i].started_seen = 0;
  R[i].done_seen = 0;
}


  uint t0 = uptime(); // referencia común de creación

  int launched = 0; 
  //bucle del padre para crear los hijos, los hijos herendan los valores del padre 
  //asi que 
  
  for (int i = 0; i < total; i++) {
    // Decidir rol según long_pos:
    // long_pos == 0 -> largo primero (i == 0)
    // long_pos == 1 -> largo ultimo (i == total-1)
    int role;
    if ((long_pos == 0 && i == 0) ||
        (long_pos == 1 && i == total - 1)) {
      role = 1; // CPU largo
    } else {
      role = 0; // CPU corto
    }

    int pid = fork();
    if (pid < 0) {
      fprintf(2, "benchsched: fork failed en i=%d\n", i);
      break;
    }
    if (pid == 0) {
      // Hijo
      close(rfd);
      send_msg(wfd, MSG_STARTED, role);

      if (role == 1) {
        work_cpu(long_ticks);
      } else {
        work_cpu(short_ticks);
      }

      send_msg(wfd, MSG_DONE, role);
      close(wfd);
      exit(0);
    } else {
      // Padre
      R[i].pid = pid;
      R[i].role = role;
      R[i].t_create = t0;
      launched++;
    }
  }

  // El padre ya no escribe en el pipe
  close(wfd);

  // Recoger 2*launched mensajes (STARTED y DONE de cada hijo)
  int need = launched * 2;
  int got = 0;
  while (got < need) {
    struct msg m;
    int rd = read(rfd, &m, sizeof(m));
    if (rd != sizeof(m)) {
      if (rd == 0)
        break; // EOF
      continue;
    }
    int idx = find_idx_by_pid(R, launched, m.pid);
    if (idx < 0)
      continue;
    if (m.type == MSG_STARTED && !R[idx].started_seen) {
      R[idx].t_start = m.t;
      R[idx].started_seen = 1;
      R[idx].role = m.role;
    } else if (m.type == MSG_DONE && !R[idx].done_seen) {
      R[idx].t_finish = m.t;
      R[idx].done_seen = 1;
      R[idx].role = m.role;
    }
    got++;
  }
  close(rfd);

  // Esperar a todos los hijos (limpieza)
  for (int i = 0; i < launched; i++)
    wait(0);

  // Cálculo de métricas
  uint sum_turn = 0, sum_resp = 0;
  int count_turn = 0, count_resp = 0;

  printf("benchsched convoy (total=%d, nshort=%d, long_pos=%s)\n",
         launched, nshort, long_pos ? "last" : "first");
  printf("pid\trole\t\tstart\tfinish\tresp\tturn\n");

  for (int i = 0; i < launched; i++) {
    uint resp = 0, turn = 0;
    if (R[i].started_seen) {
      resp = R[i].t_start - R[i].t_create;
      sum_resp += resp;
      count_resp++;
    }
    if (R[i].done_seen) {
      turn = R[i].t_finish - R[i].t_create;
      sum_turn += turn;
      count_turn++;
    }
    const char *rname = (R[i].role == 1) ? "cpu_long" : "cpu_short";
    printf("%d\t%s\t%d\t%d\t%d\t%d\n",
       R[i].pid, rname,
       R[i].t_start, R[i].t_finish, resp, turn);
  }

  if (count_resp > 0)
    printf("avg response : %u ticks\n", sum_resp / count_resp);
  if (count_turn > 0)
    printf("avg turnaround: %u ticks\n", sum_turn / count_turn);

  
  free(R);

  exit(0);
}
