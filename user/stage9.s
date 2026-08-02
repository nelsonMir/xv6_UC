.text
.globl main

main:
    addi a0, zero, 1
    beq a0, zero, done
    j external_func

done:
    ret
