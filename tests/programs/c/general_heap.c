// Heap exercise: grows the break with the brk syscall, writes a pattern
// through the allocated memory and reads it back to verify. Useful for
// exercising heap/memory configuration. Returns checksum % 256 = 224.

#include "risc-e.h"

int main(void) {
    long start = risc_e_brk(0);
    long end = risc_e_brk((void*)(start + 64));
    if (end < start + 64) return 1;  // allocation failed

    unsigned char* p = (unsigned char*)start;
    int checksum = 0;
    for (int i = 0; i < 64; ++i) {
        p[i] = (unsigned char)(i * 3 + 1);
        checksum += p[i];
    }
    for (int i = 0; i < 64; ++i) {
        if (p[i] != (unsigned char)(i * 3 + 1)) return 2;  // corruption
    }
    return checksum % 256;  // 224
}
