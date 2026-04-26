#include "hex.h"

#include "../simd.h"

#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #if defined(_MSC_VER)
        #include <stdlib.h>
        #define UUID_BSWAP64(x) _byteswap_uint64(x)
    #else
        #define UUID_BSWAP64(x) __builtin_bswap64(x)
    #endif
    #define UUID_HTOBE64(x) UUID_BSWAP64(x)
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
#if USE_SSSE3
    char tmp[32];
    simd_encode_16_hex(UUID_HTOBE64(hi), UUID_HTOBE64(lo), tmp, tmp + 16);
    memcpy(out, tmp, 8);
    out[8] = '-';
    memcpy(out + 9, tmp + 8, 4);
    out[13] = '-';
    memcpy(out + 14, tmp + 12, 4);
    out[18] = '-';
    memcpy(out + 19, tmp + 16, 4);
    out[23] = '-';
    memcpy(out + 24, tmp + 20, 12);
#else
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    hex_pair(out + 0, bytes[0]);
    hex_pair(out + 2, bytes[1]);
    hex_pair(out + 4, bytes[2]);
    hex_pair(out + 6, bytes[3]);
    out[8] = '-';
    hex_pair(out + 9, bytes[4]);
    hex_pair(out + 11, bytes[5]);
    out[13] = '-';
    hex_pair(out + 14, bytes[6]);
    hex_pair(out + 16, bytes[7]);
    out[18] = '-';
    hex_pair(out + 19, bytes[8]);
    hex_pair(out + 21, bytes[9]);
    out[23] = '-';
    hex_pair(out + 24, bytes[10]);
    hex_pair(out + 26, bytes[11]);
    hex_pair(out + 28, bytes[12]);
    hex_pair(out + 30, bytes[13]);
    hex_pair(out + 32, bytes[14]);
    hex_pair(out + 34, bytes[15]);
#endif
}

static unsigned char ascii_lower(const unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (unsigned char)(ch | 0x20) : ch;
}

static bool has_urn_uuid_prefix(const char *text, const size_t size) {
    static constexpr unsigned char prefix[] = "urn:uuid:";

    if (size < 9) {
        return false;
    }

    for (size_t i = 0; i < 9; ++i) {
        if (ascii_lower((unsigned char)text[i]) != prefix[i]) {
            return false;
        }
    }

    return true;
}

static void unwrap_uuid_text(const char **text, size_t *size) {
    if (has_urn_uuid_prefix(*text, *size)) {
        *text += 9;
        *size -= 9;
    }

    if (*size >= 2 && (*text)[0] == '{' && (*text)[*size - 1] == '}') {
        *text += 1;
        *size -= 2;
    }
}

#define PARSE_BYTE(dst, src, idx)                                                                                                          \
    do {                                                                                                                                   \
        const signed short byte_ =                                                                                                         \
            HEX_PAIR_TO_BYTE[((unsigned int)(unsigned char)((src)[(idx)]) << 8) | (unsigned int)(unsigned char)((src)[(idx) + 1])];        \
        if (byte_ < 0) {                                                                                                                   \
            return -1;                                                                                                                     \
        }                                                                                                                                  \
        (dst) = (unsigned char)byte_;                                                                                                      \
    } while (0)

#if !USE_SSSE3 && !USE_AVX2
static int parse_uuid_text_32(const char *text, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];

    PARSE_BYTE(bytes[0], text, 0);
    PARSE_BYTE(bytes[1], text, 2);
    PARSE_BYTE(bytes[2], text, 4);
    PARSE_BYTE(bytes[3], text, 6);
    PARSE_BYTE(bytes[4], text, 8);
    PARSE_BYTE(bytes[5], text, 10);
    PARSE_BYTE(bytes[6], text, 12);
    PARSE_BYTE(bytes[7], text, 14);
    PARSE_BYTE(bytes[8], text, 16);
    PARSE_BYTE(bytes[9], text, 18);
    PARSE_BYTE(bytes[10], text, 20);
    PARSE_BYTE(bytes[11], text, 22);
    PARSE_BYTE(bytes[12], text, 24);
    PARSE_BYTE(bytes[13], text, 26);
    PARSE_BYTE(bytes[14], text, 28);
    PARSE_BYTE(bytes[15], text, 30);

    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

#if USE_SSSE3 && !USE_AVX2
static int parse_uuid_text_32_ssse3(const char *text, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];

    if (simd_decode_16_hex_ssse3(text, bytes) != 0 || simd_decode_16_hex_ssse3(text + 16, bytes + 8) != 0) {
        return -1;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

#if USE_AVX2
static int parse_uuid_text_32_avx2(const char *text, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];

    if (simd_decode_32_hex_avx2(text, bytes) != 0) {
        return -1;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

int parse_uuid_hex_str(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
    char flat[32];

    unwrap_uuid_text(&text, &size);

    if (size == 36) {
        if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
            return -1;
        }

        memcpy(flat, text, 8);
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

#if USE_AVX2
    return parse_uuid_text_32_avx2(text, hi, lo);
#elif USE_SSSE3
    return parse_uuid_text_32_ssse3(text, hi, lo);
#else
    return parse_uuid_text_32(text, hi, lo);
#endif
}

#undef PARSE_BYTE
