.globl _start
_start:
    li a7, 214
    li a0, 0
    ecall              # brk(0): a0 = current break
    mv t0, a0
    li a0, 4096
    add a0, a0, t0     # a0 = break + 4096
    li a7, 214
    ecall              # brk(break + 4096)
    li t1, 123
    sw t1, 0(t0)
    lw a0, 0(t0)
    li a7, 93
    ecall
