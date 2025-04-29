#include "kernel/types.h"  //esta cabecera define los tipos de datos basicos
#include "kernel/stat.h"
#include "user/user.h" /*esta cabecera define las llamadas al sistema (aqui esta definido el sleep) 
y algunas funciones de la libreria de c, como atoi, printf */

/*Recibe 3 argumentos: comando, el pid, el delta*/
int main(int argc, char *argv[]){

     if(argc != 3){

        fprintf(2, "Uso correcto de nice: nice <PID> <delta>\n");
        //salimos con error
        exit(1);
    }

    if (!is_number(argv[2])) {
    fprintf(2,"Error: La prioridad debe ser un número válido.\n");
    exit(1);
  }

    //convierto los argumentos a numeros
    int pid = atoi(argv[1]);
    int prio = atoi(argv[2]);

  // Aquí llamarías a tu syscall nice(pid, prio)
    if (nice(pid, prio) < 0) {
        printf("Error: No se pudo cambiar la prioridad.\n");
    }

  exit(0);
}