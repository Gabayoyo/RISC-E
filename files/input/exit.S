// riscv64-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -Ttext=0x10000 files/input/exit.S -o files/output/sample.elf

.globl _start
_start:
    li a0, 0
    li a7, 93
    ecall