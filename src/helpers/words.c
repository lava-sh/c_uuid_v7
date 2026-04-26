#include "words.h"

#include <string.h>

#if defined(_MSC_VER)
    #include <stdlib.h>
    #define BSWAP64(x) _byteswap_uint64(x)
#else
    #define BSWAP64(x) __builtin_bswap64(x)
#endif

void bytes_to_words_loop(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    *hi = 0;
    *lo = 0;
    for (int i = 0; i < 8; ++i) {
        *hi = *hi << 8 | bytes[i];
        *lo = *lo << 8 | bytes[i + 8];
    }
}

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    memcpy(hi, bytes, 8);
    memcpy(lo, bytes + 8, 8);
    *hi = BSWAP64(*hi);
    *lo = BSWAP64(*lo);
}
