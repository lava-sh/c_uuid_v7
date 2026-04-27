#ifndef HEX_HEX_H
#define HEX_HEX_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint16_t HEX_PAIRS[256];
extern signed short HEX_PAIR_TO_BYTE[65536];

static inline void hex_pair(char *out, const unsigned char byte) {
    const uint16_t pair = HEX_PAIRS[byte];

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    out[0] = (char)(pair & 0xFF);
    out[1] = (char)(pair >> 8);
#else
    memcpy(out, &pair, sizeof(pair));
#endif
}

void fmt_dashed(uint64_t hi, uint64_t lo, char *out);
void fmt_hex32(uint64_t hi, uint64_t lo, char *out);

[[nodiscard]]
int parse_uuid_hex_str(const char *text, size_t size, uint64_t *hi, uint64_t *lo);

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo);
void uuid_to_bytes(uint64_t hi, uint64_t lo, unsigned char bytes[16]);
void uuid_to_bytes_le(const unsigned char *bytes, unsigned char reordered[16]);

#endif
