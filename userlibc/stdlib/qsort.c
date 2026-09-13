#include <stdint.h>
#include <stdlib.h>

static void swap(unsigned char *a, unsigned char *b, size_t size) {
    while (size--) { unsigned char c = *a; *a++ = *b; *b++ = c; }
}

static void sift(unsigned char *base, size_t root, size_t count, size_t size,
                 int (*compare)(const void *, const void *)) {
    while (root < count / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0) child++;
        if (compare(base + root * size, base + child * size) >= 0) break;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

/* Heapsort: bounded stack usage, no allocation, O(n log n) worst case. */
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    if (count < 2 || !size || count > SIZE_MAX / size) return;
    unsigned char *bytes = base;
    for (size_t i = count / 2; i; i--) sift(bytes, i - 1, count, size, compare);
    for (size_t end = count - 1; end; end--) {
        swap(bytes, bytes + end * size, size);
        sift(bytes, 0, end, size, compare);
    }
}
