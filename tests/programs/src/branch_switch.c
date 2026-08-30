// Switch dispatch: a multi-way branch (jump table or branch chain depending
// on codegen). Maps each value 0..7 to a result and sums 32 iterations.
// Returns 4 * (1+2+3+4+5+6+7+8) = 144.

static int dispatch(int v) {
    switch (v) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 3;
        case 3: return 4;
        case 4: return 5;
        case 5: return 6;
        case 6: return 7;
        default: return 8;
    }
}

int main(void) {
    int sum = 0;
    for (int i = 0; i < 32; ++i) {
        sum += dispatch(i % 8);
    }
    return sum;  // 144
}
