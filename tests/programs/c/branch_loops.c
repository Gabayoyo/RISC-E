// Predictable control flow: a fixed-trip countdown loop and a count-up
// loop, both taking the same branch directions every iteration. Sums
// 1..100 twice and returns the total divided by two (5050).

int main(void) {
    int total = 0;
    for (int i = 100; i > 0; --i) {   // countdown: backward branch, mostly taken
        total += i;
    }
    for (int i = 1; i <= 100; ++i) {  // count-up: same direction every iteration
        total += i;
    }
    return total / 2;  // 5050
}
