#include "hex.h"

#include "../simd.h"

#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #if defined(_MSC_VER)
        #include <stdlib.h>
        #define UUID_HTOBE64(x) _byteswap_uint64(x)
    #else
        #define UUID_HTOBE64(x) __builtin_bswap64(x)
    #endif
#else
    #define UUID_HTOBE64(x) (x)
#endif

void bytes_to_words(const unsigned char *bytes, uint64_t *hi, uint64_t *lo) {
    memcpy(hi, bytes, 8);
    memcpy(lo, bytes + 8, 8);
    *hi = UUID_HTOBE64(*hi);
    *lo = UUID_HTOBE64(*lo);
}

void uuid_to_bytes(const uint64_t hi, const uint64_t lo, unsigned char bytes[16]) {
    const uint64_t hi_be = UUID_HTOBE64(hi);
    const uint64_t lo_be = UUID_HTOBE64(lo);
    memcpy(bytes, &hi_be, 8);
    memcpy(bytes + 8, &lo_be, 8);
}

void uuid_to_bytes_le(const unsigned char *bytes, unsigned char reordered[16]) {
    static constexpr unsigned char order[8] = {3, 2, 1, 0, 5, 4, 7, 6};
    for (int i = 0; i < 8; ++i) {
        reordered[i] = bytes[order[i]];
    }
    memcpy(reordered + 8, bytes + 8, 8);
}

void fmt_hex32(const uint64_t hi, const uint64_t lo, char *out) {
#if USE_SSSE3
    simd_encode_16_hex(UUID_HTOBE64(hi), UUID_HTOBE64(lo), out, out + 16);
#else
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    for (int i = 0; i < 16; ++i) {
        hex_pair(out + (i * 2), bytes[i]);
    }
#endif
}

void fmt_dashed(const uint64_t hi, const uint64_t lo, char *out) {
    static constexpr int parts[] = {0, 8, 9, 4, 14, 4, 19, 4, 24, 12};

#if USE_SSSE3
    char tmp[32];
    simd_encode_16_hex(UUID_HTOBE64(hi), UUID_HTOBE64(lo), tmp, tmp + 16);
    int src = 0;
    for (int i = 0; i < 5; ++i) {
        const int dst = parts[i * 2];
        const int len = parts[i * 2 + 1];
        memcpy(out + dst, tmp + src, (size_t)len);
        src += len;
    }
#else
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    int src = 0;
    for (int i = 0; i < 5; ++i) {
        const int dst = parts[i * 2];
        const int len = parts[i * 2 + 1];
        for (int j = 0; j < len; j += 2) {
            hex_pair(out + dst + j, bytes[src++]);
        }
    }
#endif
    out[8] = '-';
    out[13] = '-';
    out[18] = '-';
    out[23] = '-';
}

static unsigned char ascii_lower(const unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (unsigned char)(ch | 0x20) : ch;
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
    for (int i = 0; i < 16; ++i) {
        const unsigned int idx = (unsigned int)(unsigned char)text[i * 2] << 8 | (unsigned int)(unsigned char)text[i * 2 + 1];
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
