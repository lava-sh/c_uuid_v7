#include "random.h"

#include "getrandom.h"

constexpr uint64_t W1RAND_C = 0xd07e'bc63'2746'54c7ULL;
constexpr uint64_t RAND_MASK62 = 0x3FFF'FFFF'FFFF'FFFFULL;
constexpr uint64_t COUNTER42_MASK = (1ULL << 42) - 1ULL;
constexpr uint64_t LOW30_MASK = (1ULL << 30) - 1ULL;

typedef struct {
    uint64_t state;
} w1rand_t;

static w1rand_t w1rand_global = {0};
static bool w1rand_seeded = false;

#ifdef __SIZEOF_INT128__
static inline uint64_t w1_mix(const uint64_t a, const uint64_t b) {
    const __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)(r >> 64) ^ (uint64_t)r;
}
#elif defined(_MSC_VER) && defined(_M_X64)
    #include <intrin.h>
    #pragma intrinsic(_umul128)
static inline uint64_t w1_mix(const uint64_t a, const uint64_t b) {
    uint64_t hi;
    _umul128(a, b, &hi);
    return hi ^ a * b;
}
#else
static inline uint64_t _wyrot(const uint64_t x) {
    return x >> 32 | x << 32;
}

static inline void _wymum(uint64_t *A, uint64_t *B) {
    const uint64_t hh = (*A >> 32) * (*B >> 32);
    const uint64_t hl = (*A >> 32) * (uint32_t)*B;
    const uint64_t lh = (uint32_t)*A * (*B >> 32);
    const uint64_t ll = (uint64_t)(uint32_t)*A * (uint32_t)*B;
    *A = _wyrot(hl) ^ hh;
    *B = _wyrot(lh) ^ ll;
}

static inline uint64_t w1_mix(uint64_t a, uint64_t b) {
    _wymum(&a, &b);
    return a ^ b;
}
#endif

static uint64_t w1_next(void) {
    w1rand_global.state += W1RAND_C;
    return w1_mix(w1rand_global.state, w1rand_global.state ^ W1RAND_C);
}

static int rnd_u64_secure(uint64_t *out) {
    if (fill_random((unsigned char *)out, sizeof(*out)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to generate random bytes");
        return -1;
    }
    return 0;
}

int random_ensure_seeded(void) {
    if (w1rand_seeded) {
        return 0;
    }

    uint64_t seed[2] = {0, 0};
    if (fill_random((unsigned char *)seed, sizeof(seed)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to seed UUIDv7 generator");
        return -1;
    }

    w1rand_global.state = seed[0] ^ w1_mix(seed[1], seed[1] ^ W1RAND_C);
    w1rand_seeded = true;
    return 0;
}

void random_reseed(void) {
    w1rand_seeded = false;
}

uint64_t random_counter42(void) {
    return w1_next() & COUNTER42_MASK;
}

int random_counter42_secure(uint64_t *counter) {
    uint64_t random64 = 0;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    *counter = random64 & COUNTER42_MASK;
    return 0;
}

void random_split_counter42(const uint64_t counter, const uint32_t low32, uint16_t *rand_a, uint64_t *tail62) {
    *rand_a = (uint16_t)(counter >> 30);
    *tail62 = (counter & LOW30_MASK) << 32 | (uint64_t)low32;
}

void random_next_low32_and_increment(uint32_t *low32, uint64_t *increment) {
    const uint64_t random64 = w1_next();

    *low32 = (uint32_t)random64;
    *increment = 1U + (random64 >> 32 & 0x0FU);
}

int random_next_low32_and_increment_secure(uint32_t *low32, uint64_t *increment) {
    uint64_t random64 = 0;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    *low32 = (uint32_t)random64;
    *increment = 1U + (random64 >> 32 & 0x0FU);
    return 0;
}

void random_payload(uint16_t *rand_a, uint64_t *tail62) {
    random_split_counter42(random_counter42(), (uint32_t)w1_next(), rand_a, tail62);
}

int random_payload_secure(uint16_t *rand_a, uint64_t *tail62) {
    uint64_t counter = 0;
    uint64_t random64 = 0;

    if (random_counter42_secure(&counter) != 0) {
        return -1;
    }
    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    random_split_counter42(counter, (uint32_t)random64, rand_a, tail62);
    return 0;
}

uint64_t random_tail62(void) {
    return w1_next() & RAND_MASK62;
}

int random_tail62_secure(uint64_t *tail62) {
    if (rnd_u64_secure(tail62) != 0) {
        return -1;
    }

    *tail62 &= RAND_MASK62;
    return 0;
}
