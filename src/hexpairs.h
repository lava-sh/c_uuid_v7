#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];

static inline void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    out[0] = (char)(pair & 0xFF);
    out[1] = (char)(pair >> 8);
#else
    memcpy(out, &pair, sizeof(pair));
#endif
}

#endif
