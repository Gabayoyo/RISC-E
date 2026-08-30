// Nested if/else chains plus an early-exit search. The classification loop
// walks a multi-level branch chain for every value, and the search loop
// exits after its first hit. Returns (count of 10..19) + (first value
// above 25) = 10 + 26 = 36.

int main(void) {
    int in_teens = 0;
    for (int i = 1; i <= 40; ++i) {
        if (i < 10) {
            // first range: nothing to count
        } else if (i < 20) {
            ++in_teens;
        } else if (i < 30) {
            // third range: nothing to count
        } else {
            // fourth range: nothing to count
        }
    }

    int first_above_25 = 0;
    for (int i = 1; i <= 40; ++i) {
        if (i > 25) {  // data-dependent: the loop stops after one hit
            first_above_25 = i;
            break;
        }
    }
    return in_teens + first_above_25;  // 36
}
