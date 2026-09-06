.equ SYS_exit, 2
.equ SYS_write, 16

.equ FD_STDOUT, 1
.equ LABEL_LEN, 8
.equ NEWLINE_LEN, 1

.data

TEXTO1:
    .asciz "abcd"

TEXTO2:
    .space 51


.rodata

LABEL1:
    .ascii "TEXTO1: "

LABEL2:
    .ascii "TEXTO2: "

NEWLINE:
    .ascii "\n"


.text

.globl _start

_start:

    # R0 de ARM -> t0
    # Direccion de TEXTO1
    la t0, TEXTO1

    # R1 de ARM -> t1
    # Contador de caracteres
    li t1, 0


WHILE:

    # Leer el caracter actual de TEXTO1
    lbu t2, 0(t0)
    # Si encontramos '\0', hemos terminado de contar
    beq t2, zero, FIN_WHILE
    # Incrementar contador
    addi t1, t1, 1
    # Avanzar al siguiente caracter
    addi t0, t0, 1

    j WHILE


FIN_WHILE:

    # En este punto t0 apunta al '\0' de TEXTO1
    # Retrocedemos una posicion para apuntar al ultimo caracter
    addi t0, t0, -1
    # Direccion inicial de TEXTO2
    la t3, TEXTO2
    # Contador del FOR
    li t4, 0


FOR:

    # Hemos copiado todos los caracteres cuando t4 == t1
    beq t4, t1, FIN_FOR
    # Leer caracter desde TEXTO1 empezando por el final
    lbu t2, 0(t0)
    # Guardarlo en TEXTO2
    sb t2, 0(t3)
    # Retroceder en TEXTO1
    addi t0, t0, -1
    # Avanzar en TEXTO2
    addi t3, t3, 1
    # Incrementar contador
    addi t4, t4, 1

    j FOR


FIN_FOR:

    # Añadir terminador nulo a TEXTO2
    sb zero, 0(t3)


    # Imprimir "TEXTO1: "

    li a0, FD_STDOUT
    la a1, LABEL1
    li a2, LABEL_LEN
    li a7, SYS_write
    ecall

    # Imprimir TEXTO1
    # t1 contiene la longitud calculada
    # por el WHILE

    li a0, FD_STDOUT
    la a1, TEXTO1
    mv a2, t1
    li a7, SYS_write
    ecall

    # Salto de línea

    li a0, FD_STDOUT
    la a1, NEWLINE
    li a2, NEWLINE_LEN
    li a7, SYS_write
    ecall

    # Imprimir "TEXTO2: "

    li a0, FD_STDOUT
    la a1, LABEL2
    li a2, LABEL_LEN
    li a7, SYS_write
    ecall

    # Imprimir TEXTO2

    li a0, FD_STDOUT
    la a1, TEXTO2
    mv a2, t1
    li a7, SYS_write
    ecall

    # Salto de línea

    li a0, FD_STDOUT
    la a1, NEWLINE
    li a2, NEWLINE_LEN
    li a7, SYS_write
    ecall

    # exit(0)

    li a0, 0
    li a7, SYS_exit
    ecall