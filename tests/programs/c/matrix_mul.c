// Matrix-arithmetic exercise: multiplies two 8x8 integer matrices whose
// entries follow closed-form patterns (a[i][j] = i + j), then validates every
// result entry against its closed form (c[i][j] = 140 + 28(i+j) + 8ij) and
// prints the result matrix. Exercises nested loops, multi-dimensional arrays
// and a heavy load/store stream. Returns the sum of all entries modulo 256.

#include "risc-e.h"

#define N 8

static int a[N][N];
static int b[N][N];
static int c[N][N];

static void u32_to_str(char* out, unsigned long v) {
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
}

static void fill_matrices(void) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            a[i][j] = i + j;
            b[i][j] = i + j;
        }
    }
}

static void multiply(void) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            int sum = 0;
            for (int k = 0; k < N; ++k) sum += a[i][k] * b[k][j];
            c[i][j] = sum;
        }
    }
}

// Sum over k of (i+k)(k+j) = sum k^2 + (i+j) sum k + ij*N, with the k range
// 0..7 giving sum k = 28 and sum k^2 = 140.
static int closed_form(int i, int j) {
    return 140 + 28 * (i + j) + 8 * i * j;
}

static void print_matrix(void) {
    char buf[128];
    for (int i = 0; i < N; ++i) {
        int n = 0;
        for (int j = 0; j < N; ++j) {
            char num[12];
            u32_to_str(num, (unsigned long)c[i][j]);
            int k = 0;
            while (num[k] != '\0') buf[n++] = num[k++];
            buf[n++] = (j == N - 1) ? '\n' : ' ';
        }
        risc_e_write(1, buf, n);
    }
}

int main(void) {
    fill_matrices();
    multiply();

    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (c[i][j] != closed_form(i, j)) return 1;
        }
    }

    print_matrix();

    unsigned long sum = 0;
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) sum += (unsigned long)c[i][j];
    }
    return (int)(sum % 256);
}
