#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];

#if defined(__GNUC__) || defined(__clang__)
#define NOT_UNUSED __attribute__((unused))
#else
#define NOT_UNUSED
#endif

static NOT_UNUSED void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

    memcpy(out, &pair, sizeof(pair));
}

#undef NOT_UNUSED

#endif
