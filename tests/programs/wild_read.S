.globl _start
_start:
    li a0, 0
    lw a0, 0(a0)       # load from address 0 -> LOAD_FAULT
    li a0, 1
    li a7, 93
    ecall
