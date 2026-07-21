#ifndef XV6_TCC_ALLOC_H
#define XV6_TCC_ALLOC_H

/*Proporciono una función con la misma interfaz que el
reallocator configurable de TinyCC*/

void *xv6_tcc_realloc(void *ptr, unsigned long size);

#endif