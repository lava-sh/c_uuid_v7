#include "hexparse.h"
#include "ascii.h"
#include "hex_nibble.h"
#include "words.h"
#include <string.h>

#ifdef HEXPARSE_USE_SIMD
#elif defined(_MSC_VER)
    #define HEXPARSE_USE_SIMD 0
#else
    #define HEXPARSE_USE_SIMD 1
#endif

#ifdef HEXPARSE_USE_SSSE3
#elif defined(__SSSE3__)
    #define HEXPARSE_USE_SSSE3 HEXPARSE_USE_SIMD
#else
    #define HEXPARSE_USE_SSSE3 0
#endif

#if HEXPARSE_USE_SSSE3
    #include <tmmintrin.h>
#endif

static signed short hex_pair_to_byte[65536];
static int hex_pair_to_byte_ready = 0;

static void init_hex_pair_table(void) {
    if (hex_pair_to_byte_ready) {
        return;
    }

    for (unsigned int i = 0; i < 65536; ++i) {
        hex_pair_to_byte[i] = -1;
    }

    for (unsigned int hi = 0; hi < 256; ++hi) {
        const int high = hex_nibble((unsigned char)hi);

        if (high < 0) {
            continue;
        }

        for (unsigned int lo = 0; lo < 256; ++lo) {
            const int low = hex_nibble((unsigned char)lo);

            if (low < 0) {
                continue;
            }

            hex_pair_to_byte[hi << 8 | lo] = (signed short)(high << 4 | low);
        }
    }

    hex_pair_to_byte_ready = 1;
}

static int decode_hex_pair(const unsigned char hi, const unsigned char lo) {
    init_hex_pair_table();
    return hex_pair_to_byte[(unsigned int)hi << 8 | lo];
}

int starts_with_urn_uuid(const char *text, const size_t size) {
    static const unsigned char prefix[] = "urn:uuid:";

    if (size < 9) {
        return 0;
    }

    for (size_t i = 0; i < 9; ++i) {
        if (ascii_lower((unsigned char)text[i]) != prefix[i]) {
            return 0;
        }
    }

    return 1;
}

static void strip_uuid_text_decorations(const char **text, size_t *size) {
    if (starts_with_urn_uuid(*text, *size)) {
        *text += 9;
        *size -= 9;
    }

    if (*size >= 2 && (*text)[0] == '{' && (*text)[*size - 1] == '}') {
        *text += 1;
        *size -= 2;
    }
}

static int parse_uuid_hex_stream(const char *text, size_t size, uint64_t *hi, uint64_t *lo, int (*hex_fn)(unsigned char)) {
    uint64_t high = 0;
    uint64_t low = 0;
    int digits = 0;

    strip_uuid_text_decorations(&text, &size);

    for (size_t i = 0; i < size; ++i) {
        const unsigned char ch = (unsigned char)text[i];
        const int nibble = ch == '-' ? -2 : hex_fn(ch);

        if (nibble == -2) {
            continue;
        }
        if (nibble < 0) {
            return -1;
        }

        if (digits < 16) {
            high = high << 4 | (uint64_t)nibble;
        } else if (digits < 32) {
            low = low << 4 | (uint64_t)nibble;
        } else {
            return -1;
        }

        digits += 1;
    }

    if (digits != 32) {
        return -1;
    }

    *hi = high;
    *lo = low;
    return 0;
}

int parse_uuid_hex_branchy(const char *text, const size_t size, uint64_t *hi, uint64_t *lo) {
    return parse_uuid_hex_stream(text, size, hi, lo, hex_nibble_branchy);
}

int parse_uuid_hex_lut(const char *text, const size_t size, uint64_t *hi, uint64_t *lo) {
    return parse_uuid_hex_stream(text, size, hi, lo, hex_nibble);
}

#define PARSE_BYTE(dst, src, idx)                                                                                                          \
    do {                                                                                                                                   \
        const int byte_ = decode_hex_pair((unsigned char)((src)[(idx)]), (unsigned char)((src)[(idx) + 1]));                               \
        if (byte_ < 0) {                                                                                                                   \
            return -1;                                                                                                                     \
        }                                                                                                                                  \
        (dst) = (unsigned char)byte_;                                                                                                      \
    } while (0)

static int parse_uuid_hex_fixed(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];

    strip_uuid_text_decorations(&text, &size);

    if (size == 32) {
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

    if (size != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return -1;
    }

    PARSE_BYTE(bytes[0], text, 0);
    PARSE_BYTE(bytes[1], text, 2);
    PARSE_BYTE(bytes[2], text, 4);
    PARSE_BYTE(bytes[3], text, 6);
    PARSE_BYTE(bytes[4], text, 9);
    PARSE_BYTE(bytes[5], text, 11);
    PARSE_BYTE(bytes[6], text, 14);
    PARSE_BYTE(bytes[7], text, 16);
    PARSE_BYTE(bytes[8], text, 19);
    PARSE_BYTE(bytes[9], text, 21);
    PARSE_BYTE(bytes[10], text, 24);
    PARSE_BYTE(bytes[11], text, 26);
    PARSE_BYTE(bytes[12], text, 28);
    PARSE_BYTE(bytes[13], text, 30);
    PARSE_BYTE(bytes[14], text, 32);
    PARSE_BYTE(bytes[15], text, 34);

    bytes_to_words(bytes, hi, lo);
    return 0;
}

#if HEXPARSE_USE_SSSE3
static int decode_16_hex_ssse3(const char *src, unsigned char *dst) {
    const __m128i input = _mm_loadu_si128((const __m128i *)src);
    const __m128i lower = _mm_or_si128(input, _mm_set1_epi8(0x20));
    const __m128i digit_mask = _mm_and_si128(_mm_cmpgt_epi8(input, _mm_set1_epi8('/')), _mm_cmpgt_epi8(_mm_set1_epi8(':'), input));
    const __m128i alpha_mask = _mm_and_si128(_mm_cmpgt_epi8(lower, _mm_set1_epi8('a' - 1)), _mm_cmpgt_epi8(_mm_set1_epi8('g'), lower));
    const __m128i valid_mask = _mm_or_si128(digit_mask, alpha_mask);
    const __m128i invalid_mask = _mm_cmpeq_epi8(valid_mask, _mm_setzero_si128());
    const __m128i digit_values = _mm_sub_epi8(input, _mm_set1_epi8('0'));
    const __m128i alpha_values = _mm_sub_epi8(lower, _mm_set1_epi8('a' - 10));
    const __m128i nibbles = _mm_or_si128(_mm_and_si128(digit_mask, digit_values), _mm_andnot_si128(digit_mask, alpha_values));
    const __m128i merged = _mm_maddubs_epi16(nibbles, _mm_set1_epi16(0x0110));
    const __m128i packed = _mm_packus_epi16(merged, _mm_setzero_si128());

    if (_mm_movemask_epi8(invalid_mask) != 0) {
        return -1;
    }

    _mm_storel_epi64((__m128i *)dst, packed);
    return 0;
}

static int parse_uuid_hex_ssse3(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];
    char flat[32];

    strip_uuid_text_decorations(&text, &size);

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

    if (decode_16_hex_ssse3(text, bytes) != 0 || decode_16_hex_ssse3(text + 16, bytes + 8) != 0) {
        return -1;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

int parse_uuid_hex(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
#if HEXPARSE_USE_SSSE3
    return parse_uuid_hex_ssse3(text, size, hi, lo);
#else
    return parse_uuid_hex_fixed(text, size, hi, lo);
#endif
}

#undef PARSE_BYTE
