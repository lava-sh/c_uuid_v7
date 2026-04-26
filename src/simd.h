#ifndef SIMD_H
#define SIMD_H

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

#include <stdint.h>

#if USE_SSSE3

static inline __m128i simd_hex_table(void) {
    return _mm_setr_epi8('0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f');
}

static inline void simd_encode_16_hex(uint64_t hi_be, uint64_t lo_be, char *out_first16, char *out_second16) {
    const __m128i input = _mm_set_epi64x((long long)lo_be, (long long)hi_be);
    const __m128i mask_low4 = _mm_set1_epi8(0x0F);
    const __m128i hi_n = _mm_and_si128(_mm_srli_epi16(input, 4), mask_low4);
    const __m128i lo_n = _mm_and_si128(input, mask_low4);
    const __m128i tbl = simd_hex_table();
    const __m128i hi_h = _mm_shuffle_epi8(tbl, hi_n);
    const __m128i lo_h = _mm_shuffle_epi8(tbl, lo_n);
    _mm_storeu_si128((__m128i *)out_first16, _mm_unpacklo_epi8(hi_h, lo_h));
    _mm_storeu_si128((__m128i *)out_second16, _mm_unpackhi_epi8(hi_h, lo_h));
}

static inline int simd_decode_16_hex_ssse3(const char *src, unsigned char *dst) {
    const __m128i input = _mm_loadu_si128((const __m128i *)src);
    const __m128i lower = _mm_or_si128(input, _mm_set1_epi8(0x20));
    const __m128i digit_mask = _mm_and_si128(_mm_cmpgt_epi8(input, _mm_set1_epi8('/')), _mm_cmpgt_epi8(_mm_set1_epi8(':'), input));
    const __m128i alpha_mask = _mm_and_si128(_mm_cmpgt_epi8(lower, _mm_set1_epi8('a' - 1)), _mm_cmpgt_epi8(_mm_set1_epi8('g'), lower));
    const __m128i invalid_mask = _mm_cmpeq_epi8(_mm_or_si128(digit_mask, alpha_mask), _mm_setzero_si128());

    if (_mm_movemask_epi8(invalid_mask) != 0) {
        return -1;
    }

    const __m128i digit_values = _mm_sub_epi8(input, _mm_set1_epi8('0'));
    const __m128i alpha_values = _mm_sub_epi8(lower, _mm_set1_epi8('a' - 10));
    const __m128i nibbles = _mm_or_si128(_mm_and_si128(digit_mask, digit_values), _mm_andnot_si128(digit_mask, alpha_values));
    const __m128i merged = _mm_maddubs_epi16(nibbles, _mm_set1_epi16(0x0110));
    const __m128i packed = _mm_packus_epi16(merged, _mm_setzero_si128());
    _mm_storel_epi64((__m128i *)dst, packed);
    return 0;
}

#endif

#if USE_AVX2

static inline int simd_decode_32_hex_avx2(const char *src, unsigned char *dst) {
    const __m256i input = _mm256_loadu_si256((const __m256i *)src);
    const __m256i lower = _mm256_or_si256(input, _mm256_set1_epi8(0x20));
    const __m256i digit_mask =
        _mm256_and_si256(_mm256_cmpgt_epi8(input, _mm256_set1_epi8('/')), _mm256_cmpgt_epi8(_mm256_set1_epi8(':'), input));
    const __m256i alpha_mask =
        _mm256_and_si256(_mm256_cmpgt_epi8(lower, _mm256_set1_epi8('a' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('g'), lower));
    const __m256i invalid_mask = _mm256_cmpeq_epi8(_mm256_or_si256(digit_mask, alpha_mask), _mm256_setzero_si256());

    if (_mm256_movemask_epi8(invalid_mask) != 0) {
        return -1;
    }

    const __m256i digit_values = _mm256_sub_epi8(input, _mm256_set1_epi8('0'));
    const __m256i alpha_values = _mm256_sub_epi8(lower, _mm256_set1_epi8('a' - 10));
    const __m256i nibbles = _mm256_or_si256(_mm256_and_si256(digit_mask, digit_values), _mm256_andnot_si256(digit_mask, alpha_values));
    const __m256i merged = _mm256_maddubs_epi16(nibbles, _mm256_set1_epi16(0x0110));
    const __m256i packed = _mm256_packus_epi16(merged, _mm256_setzero_si256());
    _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(packed));
    _mm_storel_epi64((__m128i *)(dst + 8), _mm256_extracti128_si256(packed, 1));
    return 0;
}

#endif

#endif
