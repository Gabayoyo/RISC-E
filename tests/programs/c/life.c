// Cellular-automaton exercise: Conway's Game of Life on a 24x24 toroidal
// grid, run for 40 generations from a fixed pattern of blocks, blinkers and
// gliders. Neighbour counting and the double-buffered grid update stress
// branching and the memory hierarchy. Prints the final grid and the
// live-cell count; returns the count modulo 256.

#include "risc-e.h"

#define ROWS 24
#define COLS 24
#define GENS 40

static unsigned char grid[ROWS][COLS];
static unsigned char next_grid[ROWS][COLS];

static void seed(void) {
    // Block
    grid[2][2] = 1; grid[2][3] = 1;
    grid[3][2] = 1; grid[3][3] = 1;
    // Blinker
    grid[12][4] = 1; grid[12][5] = 1; grid[12][6] = 1;
    // Glider heading down-right
    grid[5][10] = 1; grid[6][11] = 1;
    grid[7][9] = 1; grid[7][10] = 1; grid[7][11] = 1;
    // Glider heading up-left
    grid[18][18] = 1; grid[19][17] = 1; grid[19][18] = 1;
    grid[20][18] = 1; grid[20][19] = 1;
    // Block
    grid[18][2] = 1; grid[18][3] = 1;
    grid[19][2] = 1; grid[19][3] = 1;
}

static int neighbors(int r, int c) {
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            const int rr = (r + dr + ROWS) % ROWS;
            const int cc = (c + dc + COLS) % COLS;
            n += grid[rr][cc];
        }
    }
    return n;
}

static void step(void) {
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            const int n = neighbors(r, c);
            next_grid[r][c] =
                (unsigned char)((grid[r][c] && (n == 2 || n == 3)) || n == 3);
        }
    }
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) grid[r][c] = next_grid[r][c];
    }
}

static unsigned long count_live(void) {
    unsigned long live = 0;
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) live += grid[r][c];
    }
    return live;
}

static void print_grid(void) {
    char buf[COLS + 2];
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) buf[c] = grid[r][c] ? '#' : '.';
        buf[COLS] = '\n';
        risc_e_write(1, buf, COLS + 1);
    }
}

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

int main(void) {
    seed();
    for (int g = 0; g < GENS; ++g) step();

    const unsigned long live = count_live();
    print_grid();

    char buf[48];
    int i = 0;
    const char* label = "live: ";
    while (label[i] != '\0') {
        buf[i] = label[i];
        ++i;
    }
    char num[12];
    u32_to_str(num, live);
    int k = 0;
    while (num[k] != '\0') {
        buf[i++] = num[k];
        ++k;
    }
    buf[i++] = '\n';
    risc_e_write(1, buf, i);

    return (int)(live % 256);
}
