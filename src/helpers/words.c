#include "words.h"

#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #if defined(_MSC_VER)
        #include <stdlib.h>
        #define UUID_BE64(x) _byteswap_uint64(x)
    #else
        #define UUID_BE64(x) __builtin_bswap64(x)
    #endif
#else
    #define UUID_BE64(x) (x)
#endif

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    memcpy(hi, bytes, 8);
    memcpy(lo, bytes + 8, 8);
    *hi = UUID_BE64(*hi);
    *lo = UUID_BE64(*lo);
}
