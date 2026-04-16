#include "words.h"

void bytes_to_words_loop(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    *hi = 0;
    *lo = 0;
    for (int i = 0; i < 8; ++i) {
        *hi = *hi << 8 | bytes[i];
        *lo = *lo << 8 | bytes[i + 8];
    }
}

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    *hi = (uint64_t)bytes[0] << 56 | (uint64_t)bytes[1] << 48 | (uint64_t)bytes[2] << 40 | (uint64_t)bytes[3] << 32 |
          (uint64_t)bytes[4] << 24 | (uint64_t)bytes[5] << 16 | (uint64_t)bytes[6] << 8 | (uint64_t)bytes[7];
    *lo = (uint64_t)bytes[8] << 56 | (uint64_t)bytes[9] << 48 | (uint64_t)bytes[10] << 40 | (uint64_t)bytes[11] << 32 |
          (uint64_t)bytes[12] << 24 | (uint64_t)bytes[13] << 16 | (uint64_t)bytes[14] << 8 | (uint64_t)bytes[15];
}
