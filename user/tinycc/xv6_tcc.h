#ifndef XV6_TCC_H
#define XV6_TCC_H

/*Valores devueltos por el port del compilador tinyCC, aquí se pondrán 
los valores a devolver cuando ocurre un error del ensamblador. 
*/
#define XV6_TCC_OK             0
#define XV6_TCC_ERR_INPUT     -1
#define XV6_TCC_ERR_NOT_READY -2
#define XV6_TCC_ERR_STAT      -3
#define XV6_TCC_ERR_READ      -4
#define XV6_TCC_ERR_MEMORY    -5
#define XV6_TCC_ERR_TOO_LARGE -6
#define XV6_TCC_ERR_TOKEN_TABLE -7
#define XV6_TCC_ERR_ELF_ABI   -8
#define XV6_TCC_ERR_BACKEND   -9
#define XV6_TCC_ERR_SECTION   -10

 /*Esta función ensambla input_path (el fichero en esa dirección) y escribirá 
 un ELF relocatable en output_path. 
 Esta función será el punto de entrada del emsamblador TInyCC adaptado para xv6, ósea
 la usaré como interfaz entre el asxv6.c y el TinyCC adaptado de forma que el compilador de 
 xv6 no se tenga que enterar de las estructuras internas de TinyCC que se utilizarán del compilador original*/
int xv6_tcc_assemble(const char *input_path,
                     const char *output_path);

#endif