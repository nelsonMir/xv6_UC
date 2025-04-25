#include "kernel/types.h"  //esta cabecera define los tipos de datos basicos
#include "kernel/stat.h"
#include "user/user.h" /*esta cabecera define las llamadas al sistema (aqui esta definido el sleep) 
y algunas funciones de la libreria de c, como atoi, printf */

int main(int argc, char *argv[]){

    //hacemos la llamada al stub
    int memoria_libre = freemem();
    int tamanhio_pag = pagesize();

    printf("Available memory in bytes = %d\n",memoria_libre);
    printf("Available pages = %d\n",memoria_libre/tamanhio_pag);
    exit(0);
}