// Sorting exercise: quicksort over a 64-entry array of pseudo-random values
// from a fixed linear congruential generator, cross-checked against an
// insertion sort of the same input. The recursion exercises call/return
// prediction; the swap-heavy partition loop and the two passes over the
// array exercise the memory hierarchy. Returns the checksum of the sorted
// array as the exit code.

#include "risc-e.h"

#define SIZE 64

static unsigned int lcg_state = 12345u;

static unsigned int lcg_next(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;  // wraps mod 2^32
    return lcg_state;
}

static void quicksort(int* arr, int lo, int hi) {
    if (lo >= hi) return;
    const int pivot = arr[(lo + hi) / 2];
    int i = lo;
    int j = hi;
    while (i <= j) {
        while (arr[i] < pivot) ++i;
        while (arr[j] > pivot) --j;
        if (i <= j) {
            const int tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            ++i;
            --j;
        }
    }
    quicksort(arr, lo, j);
    quicksort(arr, i, hi);
}

static void insertion_sort(int* arr, int n) {
    for (int i = 1; i < n; ++i) {
        const int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

int main(void) {
    int arr[SIZE];
    int reference[SIZE];
    for (int i = 0; i < SIZE; ++i) {
        arr[i] = (int)(lcg_next() % 1000u);
        reference[i] = arr[i];
    }

    quicksort(arr, 0, SIZE - 1);
    insertion_sort(reference, SIZE);

    int ok = 1;
    unsigned long sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        if (arr[i] != reference[i]) ok = 0;
        sum += (unsigned long)arr[i];
    }
    if (!ok) return 1;

    return (int)(sum % 256);
}
