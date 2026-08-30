// Exercises the C-input path end to end: the tool compiles this file on the
// fly with a bundled freestanding crt0 + risc-e.h, prints through the
// emulated write syscall and exits with main's return value (42).

#include "risc-e.h"

int main(void) {
    risc_e_write(1, "Hello from C\n", 13);
    return 42;
}
