#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>

extern const uint16_t HEX_PAIRS[256];

static inline void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

    out[0] = (char)(pair & 0xFF);
    out[1] = (char)(pair >> 8);
}

#endif
