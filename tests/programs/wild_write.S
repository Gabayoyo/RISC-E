.globl _start
_start:
    li t0, 0
    li t1, 1
    sw t1, 0(t0)       # store to address 0 -> STORE_FAULT
    li a0, 1
    li a7, 93
    ecall
