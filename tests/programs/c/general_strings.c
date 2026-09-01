// Text-processing exercise: a small freestanding string library (length,
// reverse, space compaction, integer-to-string) over string literals and
// stack buffers, plus three classic checks over a corpus of sentences: word
// counts, vowel counts and palindrome detection (spaces ignored, case
// folded). Prints a report through the emulated write syscall. Returns a
// weighted checksum of the totals as the exit code.

#include "risc-e.h"

static unsigned long str_len(const char* s) {
    unsigned long n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

// Reverses src into dst (dst must hold len + 1 bytes).
static void str_reverse(char* dst, const char* src, unsigned long len) {
    for (unsigned long i = 0; i < len; ++i) dst[i] = src[len - 1 - i];
    dst[len] = '\0';
}

// Writes the non-space characters of src to dst; returns the compacted length.
static unsigned long compact(char* dst, const char* src, unsigned long len) {
    unsigned long n = 0;
    for (unsigned long i = 0; i < len; ++i) {
        if (src[i] != ' ') dst[n++] = src[i];
    }
    return n;
}

// A sentence is a palindrome when its letters read the same in reverse
// (spaces dropped, case folded).
static int is_palindrome(const char* s) {
    char buf[64];
    char rev[64];
    unsigned long n = compact(buf, s, str_len(s));
    for (unsigned long i = 0; i < n; ++i) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] + 32);
    }
    str_reverse(rev, buf, n);
    for (unsigned long i = 0; i < n; ++i) {
        if (buf[i] != rev[i]) return 0;
    }
    return 1;
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

static void write_u32_line(const char* label, unsigned long v) {
    char buf[48];
    int i = 0;
    while (label[i] != '\0') {
        buf[i] = label[i];
        ++i;
    }
    char num[12];
    u32_to_str(num, v);
    int k = 0;
    while (num[k] != '\0') {
        buf[i++] = num[k];
        ++k;
    }
    buf[i++] = '\n';
    risc_e_write(1, buf, i);
}

static const char* kSentences[] = {
    "the quick brown fox jumps over the lazy dog",
    "racecar",
    "never odd or even",
    "hello world this is risc e",
    "a man a plan a canal panama",
};

int main(void) {
    unsigned long total_words = 0;
    unsigned long total_letters = 0;
    unsigned long total_vowels = 0;
    unsigned long palindromes = 0;

    for (unsigned long s = 0; s < sizeof(kSentences) / sizeof(kSentences[0]); ++s) {
        const char* sentence = kSentences[s];
        const unsigned long len = str_len(sentence);
        unsigned long words = 1;
        unsigned long letters = 0;
        unsigned long vowels = 0;
        for (unsigned long i = 0; i < len; ++i) {
            if (sentence[i] == ' ') {
                ++words;
            } else if (is_letter(sentence[i])) {
                ++letters;
                if (is_vowel(sentence[i])) ++vowels;
            }
        }
        total_words += words;
        total_letters += letters;
        total_vowels += vowels;
        if (is_palindrome(sentence)) ++palindromes;
    }

    write_u32_line("words: ", total_words);
    write_u32_line("letters: ", total_letters);
    write_u32_line("vowels: ", total_vowels);
    write_u32_line("palindromes: ", palindromes);

    const unsigned long checksum =
        total_words + 2 * total_letters + 3 * total_vowels + 5 * palindromes;
    return (int)(checksum % 256);
}
