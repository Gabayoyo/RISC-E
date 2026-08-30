.globl _start
_start:
    li a0, 7
    addi sp, sp, -4
    sw a0, 0(sp)
    lw a0, 0(sp)
    li a7, 93
    ecall
