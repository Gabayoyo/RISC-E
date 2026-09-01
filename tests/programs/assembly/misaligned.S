.globl _start
_start:
    li t0, 0x10004
    lw a0, 1(t0)       # misaligned load -> LOAD_MISALIGNED
    li a0, 1
    li a7, 93
    ecall
