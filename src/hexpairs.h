#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];

static void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

    memcpy(out, &pair, sizeof(pair));
}

#endif
