.equ SYS_exit,2
.equ SYS_write,16
.equ FD_STDOUT,1
.equ TEXT_LEN,4
.equ LABEL_LEN,8
.equ NEWLINE_LEN,1
.data
TEXTO1:
.asciz "abcd"
.space 46
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
la t0,TEXTO1
li t1,0
strlen_loop:
lbu t2,0(t0)
beq t2,zero,strlen_done
addi t1,t1,1
addi t0,t0,1
j strlen_loop
strlen_done:
addi t0,t0,-1
la t3,TEXTO2
li t4,0
reverse_loop:
beq t4,t1,reverse_done
lbu t2,0(t0)
sb t2,0(t3)
addi t0,t0,-1
addi t3,t3,1
addi t4,t4,1
j reverse_loop
reverse_done:
sb zero,0(t3)
li a0,FD_STDOUT
la a1,LABEL1
li a2,LABEL_LEN
li a7,SYS_write
ecall
li a0,FD_STDOUT
la a1,TEXTO1
li a2,TEXT_LEN
li a7,SYS_write
ecall
li a0,FD_STDOUT
la a1,NEWLINE
li a2,NEWLINE_LEN
li a7,SYS_write
ecall
li a0,FD_STDOUT
la a1,LABEL2
li a2,LABEL_LEN
li a7,SYS_write
ecall
li a0,FD_STDOUT
la a1,TEXTO2
li a2,TEXT_LEN
li a7,SYS_write
ecall
li a0,FD_STDOUT
la a1,NEWLINE
li a2,NEWLINE_LEN
li a7,SYS_write
ecall
li a0,0
li a7,SYS_exit
ecall