#include "kernel/types.h"  //esta cabecera define los tipos de datos basicos
#include "kernel/stat.h"
#include "user/user.h" /*esta cabecera define las llamadas al sistema (aqui esta definido el sleep) 
y algunas funciones de la libreria de c, como atoi, printf */

//no hay studio.h ya que xv6 no tiene la libc, la libreria de c

/*los programas de C de usuario (ls, cat...) deben ir en el directorio user, y debemos agregarlos 
al makefile en la variable "UPROGS" para que el "make qemu" compile este programa en c y lo instale 
en el sistema de fichero de xv6 */

/*Escribimos "const char" para indicar explicitamente al compilador que no modificaremos el contenido 
del puntero a string, osea que no modificaremos el contenido del string, y esto nos permite evitar
errores de redefinir casillas del array y tambien permite pasar constantes como argumento sin advertencias, 
por ejemplo "is_number("123")"*/
int is_number_sleep(const char *); //va a verificar que todos los caracteres del string sean digitos del 1 al 9

//arg es el # de argumentos, y argv es el array con los argumentos
int main(int argc, char *argv[]){

    //Si el numero de argumentos es diferente a 2 entonces error
    if(argc != 2){

        //recuerda, fprintf esta definido en xv6 y primero va el canal y luego el mensaje, 1 es a stdout 
        //y 2 es para stder. si quisiera hacer siempre frpintf(1,..) puedo hacer simplemente printf(..)
        //que es lo mismo
        fprintf(2, "Uso correcto de sleep: sleep <num de ticks>\n");
        //salimos con error
        exit(1);
    }

    //vamos a comprobar que el argumento es un numero 
    if(is_number(argv[1]) == 0){
        //si no es un numero, entonces entramos aqui
        fprintf(2, "Error: argumento no numérico\n");
        //salimos con error
        exit(1);
    }

    //sacamos el número de ticks, para ello convertimos el ASCII en entero
    int tick = atoi(argv[1]);
    printf("A dormirse %d ticks...\n", tick);
    sleep(tick);
    //salimos correctamente
    exit(0);
}

int is_number_sleep(const char *s){

    //vamos a recorrer todos los caracteres del string y nos detenemos hasta encontrar el caracter nulo
    for(int i = 0; s[i] != 0; i++){
        //vamos a comparar si el caracter actual es un digito del 1 al 9, las letras ascii tienen un 
        //num diferente asignado asi que es por eso que los podemos diferenciar
        if(s[i] < '0' || s[i] > '9'){
            //si entra aqui es que no es un numero
            return 0; //no es un numero
        }
    }

    //si se llega hasta aqui, entonces cada caracter es un digito 
    return 1; //es un numero
}