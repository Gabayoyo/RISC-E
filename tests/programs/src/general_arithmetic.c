// General arithmetic: signed/unsigned division and modulo (libgcc helpers),
// shifts, and comparisons. Useful as a smoke test for ALU and libgcc
// support. Returns the low byte of a fixed checksum (7).

int main(void) {
    int a = 2024 / 7;                 // 289
    int b = 2024 % 7;                 // 1
    int c = -2024 / 7;                // -289 (signed, truncates toward zero)
    int d = (int)((unsigned)-1 >> 4); // 0x0FFFFFFF (logical shift)
    int e = 1 << 20;                  // 1048576
    int f = (a < 0) ? 3 : 4;          // 4
    unsigned u = 0xFFFFFFFFu;
    int g = (u > 2024u) ? 5 : 6;      // 5 (unsigned comparison)
    return (a + b + c + d + e + f + g) & 0xFF;  // 9
}
