#ifndef HEX_PAIRS_H
#define HEX_PAIRS_H

#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif

static void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

    memcpy(out, &pair, sizeof(pair));
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
