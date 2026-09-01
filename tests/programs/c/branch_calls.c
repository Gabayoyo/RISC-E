// Call-heavy control flow: a direct helper call plus a recursive Fibonacci.
// Every call/return pair is a JAL/JALR control transfer, exercising the
// predictors' call/return handling (RAS). Returns 34 + 55 - 34 = 55.

static int add3(int a, int b, int c) {
    return a + b + c;
}

static int fib(int n) {
    if (n < 2) return n;              // data-dependent branch per frame
    return fib(n - 1) + fib(n - 2);   // two recursive calls per frame
}

int main(void) {
    return add3(fib(9), fib(10), -fib(9));
}
