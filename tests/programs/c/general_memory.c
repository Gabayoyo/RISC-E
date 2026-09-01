// General memory exercise: reads a .data array, verifies a .bss array was
// zero-filled by the loader, and writes/reads a stack array. Useful for
// exercising memory configuration and layout. Returns checksum % 1000 = 108.

static int data_arr[8] = {1, 2, 3, 4, 5, 6, 7, 8};  // .data (initialized)
static int bss_arr[8];                              // .bss (must be zero)

int main(void) {
    int stack_arr[8];  // stack
    int checksum = 0;
    for (int i = 0; i < 8; ++i) {
        stack_arr[i] = data_arr[i] * 2;
        checksum += data_arr[i];
        checksum += bss_arr[i];   // loader must have zero-filled this
        checksum += stack_arr[i];
    }
    return checksum % 1000;  // 36 + 0 + 72 = 108
}
