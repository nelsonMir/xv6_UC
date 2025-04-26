	#include "kernel/types.h"
	#include "user/user.h"
	
//arg es el # de argumentos, y argv es el array con los argumentos
int main(int argc, char *argv[]){

    int prioridad;
    int pid;

    //Si el numero de argumentos es diferente a 2 entonces error
    if(argc != 2){

        //recuerda, fprintf esta definido en xv6 y primero va el canal y luego el mensaje, 1 es a stdout 
        //y 2 es para stder. si quisiera hacer siempre frpintf(1,..) puedo hacer simplemente printf(..)
        //que es lo mismo
        fprintf(2, "Uso correcto de getpriority: getpriority <PID>\n");
        //salimos con error
        exit(1);
    }

    //convertimos el argumento a un numero entero
    pid = atoi(argv[1]);

    prioridad = getpriority(pid);

    //si recibimos una prioridad menor a 20 es porque no se encontró el proceso
    if(prioridad < -20){
        fprintf(2, "No se encontró el proceso con PID %d\n", pid);
        exit(1);
    }

    printf("Prioridad %d\n",prioridad);

}