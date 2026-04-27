#include "hex.h"

#include "../simd.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define HTOBE64(x) __builtin_bswap64(x)
#else
    #define HTOBE64(x) (x)
#endif

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    memcpy(hi, bytes, 8);
    memcpy(lo, bytes + 8, 8);
    *hi = HTOBE64(*hi);
    *lo = HTOBE64(*lo);
}

void uuid_to_bytes(const uint64_t hi, const uint64_t lo, unsigned char bytes[16]) {
    const uint64_t hi_be = HTOBE64(hi);
    const uint64_t lo_be = HTOBE64(lo);
    memcpy(bytes, &hi_be, 8);
    memcpy(bytes + 8, &lo_be, 8);
}

void uuid_to_bytes_le(const unsigned char *bytes, unsigned char reordered[16]) {
    reordered[0] = bytes[3];
    reordered[1] = bytes[2];
    reordered[2] = bytes[1];
    reordered[3] = bytes[0];
    reordered[4] = bytes[5];
    reordered[5] = bytes[4];
    reordered[6] = bytes[7];
    reordered[7] = bytes[6];
    memcpy(reordered + 8, bytes + 8, 8);
}

void fmt_hex32(const uint64_t hi, const uint64_t lo, char *out) {
#if USE_SSSE3
    simd_encode_16_hex(HTOBE64(hi), HTOBE64(lo), out, out + 16);
#else
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    for (ptrdiff_t i = 0; i < 16; ++i) {
        hex_pair(out + (i * 2), bytes[i]);
    }
#endif
}

void fmt_dashed(const uint64_t hi, const uint64_t lo, char *out) {
#if USE_SSSE3
    char tmp[32];
    simd_encode_16_hex(HTOBE64(hi), HTOBE64(lo), tmp, tmp + 16);
    memcpy(out + 0, tmp + 0, 8);
    memcpy(out + 9, tmp + 8, 4);
    memcpy(out + 14, tmp + 12, 4);
    memcpy(out + 19, tmp + 16, 4);
    memcpy(out + 24, tmp + 20, 12);
#else
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    hex_pair(out + 0, bytes[0]);
    hex_pair(out + 2, bytes[1]);
    hex_pair(out + 4, bytes[2]);
    hex_pair(out + 6, bytes[3]);
    hex_pair(out + 9, bytes[4]);
    hex_pair(out + 11, bytes[5]);
    hex_pair(out + 14, bytes[6]);
    hex_pair(out + 16, bytes[7]);
    hex_pair(out + 19, bytes[8]);
    hex_pair(out + 21, bytes[9]);
    hex_pair(out + 24, bytes[10]);
    hex_pair(out + 26, bytes[11]);
    hex_pair(out + 28, bytes[12]);
    hex_pair(out + 30, bytes[13]);
    hex_pair(out + 32, bytes[14]);
    hex_pair(out + 34, bytes[15]);
#endif
    out[8] = '-';
    out[13] = '-';
    out[18] = '-';
    out[23] = '-';
}

static unsigned char ascii_lower(const unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch | 0x20) : ch;
}

static void unwrap_uuid_text(const char **text, size_t *size) {
    static constexpr unsigned char prefix[] = "urn:uuid:";

    if (*size >= 9) {
        bool match = true;
        for (size_t i = 0; i < 9; ++i) {
            if (ascii_lower((unsigned char)(*text)[i]) != prefix[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            *text += 9;
            *size -= 9;
        }
    }

    if (*size >= 2 && (*text)[0] == '{' && (*text)[*size - 1] == '}') {
        *text += 1;
        *size -= 2;
    }
}

static int decode_hex_32(const char *text, unsigned char bytes[16]) {
#if USE_AVX2
    return simd_decode_32_hex_avx2(text, bytes);
#elif USE_SSSE3
    if (simd_decode_16_hex_ssse3(text, bytes) != 0) {
        return -1;
    }
    return simd_decode_16_hex_ssse3(text + 16, bytes + 8);
#else
    for (ptrdiff_t i = 0; i < 16; ++i) {
        const ptrdiff_t offset = i * 2;
        const unsigned int idx = (unsigned int)(unsigned char)text[offset] << 8 | (unsigned int)(unsigned char)text[offset + 1];
        const signed short v = HEX_PAIR_TO_BYTE[idx];
        if (v < 0) {
            return -1;
        }
        bytes[i] = (unsigned char)v;
    }
    return 0;
#endif
}

int parse_uuid_hex_str(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
    char flat[32];
    unsigned char bytes[16];

    unwrap_uuid_text(&text, &size);

    if (size == 36) {
        if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
            return -1;
        }
        memcpy(flat + 0, text + 0, 8);
        memcpy(flat + 8, text + 9, 4);
        memcpy(flat + 12, text + 14, 4);
        memcpy(flat + 16, text + 19, 4);
        memcpy(flat + 20, text + 24, 12);
        text = flat;
        size = 32;
    }

    if (size != 32) {
        return -1;
    }

    if (decode_hex_32(text, bytes) != 0) {
        return -1;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}
