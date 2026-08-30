// Branch-prediction showcase written in C: counts the primes up to 200
// (46 of them). The outer loop is a predictable countdown while the inner
// trial division stops early on divisors, so the data-dependent branches
// are hard to predict — a good workout for the simulated predictors.
// Division and modulo use libgcc helpers (__divsi3/__modsi3), which the
// tool links via trailing -lgcc.

static int is_prime(int n) {
    if (n < 2) return 0;
    for (int d = 2; d * d <= n; ++d) {
        if (n % d == 0) return 0;
    }
    return 1;
}

int main(void) {
    int count = 0;
    for (int i = 2; i <= 200; ++i) {
        if (is_prime(i)) ++count;
    }
    return count;
}
