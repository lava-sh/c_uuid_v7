#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];

#if defined(__GNUC__) || defined(__clang__)
#define C_UUID_V7_ __attribute__((unused))
#else
#define C_UUID_V7_
#endif

static C_UUID_V7_ void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

    memcpy(out, &pair, sizeof(pair));
}

#undef C_UUID_V7_

#endif
