// Data-dependent branches: whether an element is even is not known until
// runtime, so the branch direction alternates irregularly. Counts the even
// entries of a fixed array and returns the count (10).

static const int values[20] = {2, 7, 4, 1, 9, 6, 3, 8, 5, 10,
                               14, 11, 16, 13, 18, 15, 20, 17, 12, 19};

int main(void) {
    int evens = 0;
    for (int i = 0; i < 20; ++i) {
        if (values[i] % 2 == 0) ++evens;  // direction depends on the data
    }
    return evens;  // 10
}
