#pragma once

// Embedded freestanding runtime for C programs compiled on the fly by the
// CLI. Both files are written to a per-process temp directory next to the
// output ELF, so the compile is self-contained and does not depend on where
// the risc-e binary was installed.
//
// The cross-GCC preprocesses `.S` files before assembling them, so the crt0
// uses `//` comments (a `#` comment would be mistaken for a cpp directive).

namespace {

// Minimal _start for freestanding C programs: calls main() with argc/argv
// cleared and exits with main's return value via the emulated exit(93)
// syscall. sp is left as the interpreter initializes it (0x80000000), the
// same convention the assembly test programs rely on.
constexpr char kCrt0Source[] = R"asm(
    .section .text
    .globl _start
_start:
    li   a0, 0          // argc
    li   a1, 0          // argv
    call main
    li   a7, 93         // SYS_exit
    ecall
1:  j    1b             // safety net if the exit syscall is not handled
)asm";

// Freestanding syscall wrappers for C programs. Only the three emulated
// Linux RISC-V syscalls exist: write (64), exit (93) and brk (214).
constexpr char kRuntimeHeader[] = R"h(
#ifndef RISC_E_RUNTIME_H
#define RISC_E_RUNTIME_H

static long risc_e_syscall3(long number, long arg0, long arg1, long arg2) {
    register long a7 __asm__("a7") = number;
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}

static long risc_e_write(int fd, const void* buf, unsigned long count) {
    return risc_e_syscall3(64, fd, (long)buf, (long)count);
}

static long risc_e_brk(void* addr) {
    return risc_e_syscall3(214, (long)addr, 0, 0);
}

static void risc_e_exit(long code) {
    risc_e_syscall3(93, code, 0, 0);
    for (;;) {}
}

#endif
)h";

} // namespace
