// Exercises the branch predictor: a countdown loop (conditional branches),
// a direct call (JAL) and an indirect return (JALR).
// Expected behavior: loops 5 times (4 taken + 1 not-taken branch),
// then calls twice() via JAL and returns via JALR; exits with code 7.

.globl _start
_start:
    li t0, 5
loop:
    addi t0, t0, -1
    bne t0, zero, loop
    jal ra, twice
    li a7, 93
    ecall
twice:
    li a0, 7
    jalr zero, 0(ra)
