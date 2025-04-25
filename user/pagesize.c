#include "kernel/types.h"  //esta cabecera define los tipos de datos basicos
#include "kernel/stat.h"
#include "user/user.h" /*esta cabecera define las llamadas al sistema (aqui esta definido el sleep) 
y algunas funciones de la libreria de c, como atoi, printf */


int main(int argc, char *argv[]){

    //hacemos la llamada al stub
    int tamanhio_pag = pagesize();
    printf("%d bytes\n",tamanhio_pag);
    exit(0);
}