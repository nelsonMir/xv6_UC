#ifndef XV6_TCC_H
#define XV6_TCC_H

/*Valores devueltos por el port del compilador tinyCC, aquí se pondrán 
los valores a devolver cuando ocurre un error del ensamblador. 
De momento solo comprueba si se tiene un fichero .s*/
#define XV6_TCC_OK             0
#define XV6_TCC_ERR_INPUT     -1
#define XV6_TCC_ERR_NOT_READY -2

 /*Esta función ensambla input_path (el fichero en esa dirección) y escribirá 
 un ELF relocatable en output_path. 
 Esta función será el punto de entrada del emsamblador TInyCC adaptado para xv6, ósea
 la usaré como interfaz entre el asxv6.c y el TinyCC adaptado de forma que el compilaro de 
 xv6 no se tenga que enterar de las estructuras internas de TinyCC que se utilizarán del compilador original*/
int xv6_tcc_assemble(const char *input_path,
                     const char *output_path);

#endif