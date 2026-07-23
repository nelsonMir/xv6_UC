#ifndef XV6_TCC_BACKEND_H
#define XV6_TCC_BACKEND_H

// Se comprueban las definiciones básicas del backend RISC-V de TinyCC
int xv6_tcc_check_riscv_backend(void);

// Se devuelve el número de registros reconocidos por el backend
int xv6_tcc_backend_register_count(void);

#endif