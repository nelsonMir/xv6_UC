#ifndef XV6_TCC_ENTRY_H
#define XV6_TCC_ENTRY_H

/*Este fichero va a declarar las llamadas (define la API) para conectar los programas de usuario del ensamblador y linker 
con los cores/núcleos del ensamblador y linker reales basados en TinyCC

Cabe mencionar que por orden voy  a hacer que los programas de usuario del ensamblador y linker nunca llamen directamente a los 
cores, sino que llamarán a funciones definidas en el "_entry" en donde estará el wrapper/stub el cual hará la verdadera llamada a los cores*/
int xv6_tcc_assemble_file(char *input, char *output);
int xv6_tcc_link_files(int input_count, char **inputs, char *output);

#endif
