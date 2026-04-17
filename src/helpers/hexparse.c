#include "hexparse.h"
#include "ascii.h"
#include "hex_decode.h"
#include "words.h"
#include <string.h>

#ifdef USE_SIMD
#elif defined(_MSC_VER)
    #define USE_SIMD 0
#else
    #define USE_SIMD 1
#endif

#ifdef USE_SSSE3
#elif defined(__SSSE3__)
    #define USE_SSSE3 USE_SIMD
#else
    #define USE_SSSE3 0
#endif

#ifdef USE_AVX2
#elif defined(__AVX2__)
    #define USE_AVX2 USE_SIMD
#else
    #define USE_AVX2 0
#endif

#if USE_SSSE3 || USE_AVX2
    #include <immintrin.h>
#endif

static signed short hex_pair_to_byte[65536];
static int hex_pair_table_ready = 0;

static void init_hex_pair_table(void) {
    if (hex_pair_table_ready) {
        return;
    }

    for (unsigned int i = 0; i < 65536; ++i) {
        hex_pair_to_byte[i] = -1;
    }

    for (unsigned int hi = 0; hi < 256; ++hi) {
        const int high = hex_decode((unsigned char)hi);

        if (high < 0) {
            continue;
        }

        for (unsigned int lo = 0; lo < 256; ++lo) {
            const int low = hex_decode((unsigned char)lo);

            if (low < 0) {
                continue;
            }

            hex_pair_to_byte[hi << 8 | lo] = (signed short)(high << 4 | low);
        }
    }

    hex_pair_table_ready = 1;
}

static int decode_hex_pair(const unsigned char hi, const unsigned char lo) {
    init_hex_pair_table();
    return hex_pair_to_byte[(unsigned int)hi << 8 | lo];
}

static int has_urn_uuid_prefix(const char *text, const size_t size) {
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
        const int byte_ = decode_hex_pair((unsigned char)((src)[(idx)]), (unsigned char)((src)[(idx) + 1]));                               \
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

#if USE_SSSE3
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

static int parse_uuid_text_32_ssse3(const char *text, uint64_t *hi, uint64_t *lo) {
    unsigned char bytes[16];

    if (decode_16_hex_ssse3(text, bytes) != 0 || decode_16_hex_ssse3(text + 16, bytes + 8) != 0) {
        return -1;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

#if USE_AVX2
static int parse_uuid_text_32_avx2(const char *text, uint64_t *hi, uint64_t *lo) {
    const __m256i input = _mm256_loadu_si256((const __m256i *)text);
    const __m256i lower = _mm256_or_si256(input, _mm256_set1_epi8(0x20));
    const __m256i digit_mask =
        _mm256_and_si256(_mm256_cmpgt_epi8(input, _mm256_set1_epi8('/')), _mm256_cmpgt_epi8(_mm256_set1_epi8(':'), input));
    const __m256i alpha_mask =
        _mm256_and_si256(_mm256_cmpgt_epi8(lower, _mm256_set1_epi8('a' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('g'), lower));
    const __m256i valid_mask = _mm256_or_si256(digit_mask, alpha_mask);
    const __m256i invalid_mask = _mm256_cmpeq_epi8(valid_mask, _mm256_setzero_si256());
    const __m256i digit_values = _mm256_sub_epi8(input, _mm256_set1_epi8('0'));
    const __m256i alpha_values = _mm256_sub_epi8(lower, _mm256_set1_epi8('a' - 10));
    const __m256i nibbles = _mm256_or_si256(_mm256_and_si256(digit_mask, digit_values), _mm256_andnot_si256(digit_mask, alpha_values));
    const __m256i merged = _mm256_maddubs_epi16(nibbles, _mm256_set1_epi16(0x0110));
    const __m256i packed = _mm256_packus_epi16(merged, _mm256_setzero_si256());
    __m128i lo_bytes;
    __m128i hi_bytes;
    unsigned char bytes[16];

    if (_mm256_movemask_epi8(invalid_mask) != 0) {
        return -1;
    }

    lo_bytes = _mm256_castsi256_si128(packed);
    hi_bytes = _mm256_extracti128_si256(packed, 1);
    _mm_storel_epi64((__m128i *)bytes, lo_bytes);
    _mm_storel_epi64((__m128i *)(bytes + 8), hi_bytes);
    bytes_to_words(bytes, hi, lo);
    return 0;
}
#endif

int parse_uuid_hex(const char *text, size_t size, uint64_t *hi, uint64_t *lo) {
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
