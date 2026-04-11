#include "random.h"

#include "platform.h"

#define WYRAND_INCREMENT 0x2d358dccaa6c78a5ULL
#define WYRAND_MIX_CONST 0x8bb84b93962eacc9ULL
#define RAND_MASK62 0x3FFFFFFFFFFFFFFFULL
#define COUNTER42_MASK ((1ULL << 41) - 1ULL)
#define LOW30_MASK ((1ULL << 30) - 1ULL)

typedef struct {
    uint64_t state;
} wyrand_t;

static wyrand_t wyrand_global = {0};
static int wyrand_seeded = 0;

static uint64_t unpack_u64_be(const unsigned char bytes[8]) {
    return (uint64_t)bytes[0] << 56 | (uint64_t)bytes[1] << 48 | (uint64_t)bytes[2] << 40 |
           (uint64_t)bytes[3] << 32 | (uint64_t)bytes[4] << 24 | (uint64_t)bytes[5] << 16 |
           (uint64_t)bytes[6] << 8 | (uint64_t)bytes[7];
}

#if defined(__SIZEOF_INT128__)
static inline uint64_t wy_mix(uint64_t a, uint64_t b) {
    __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)(r >> 64) ^ (uint64_t)r;
}
#elif defined(_MSC_VER)
    #include <intrin.h>
    #pragma intrinsic(_umul128)
static inline uint64_t wy_mix(uint64_t a, uint64_t b) {
    uint64_t hi;
    _umul128(a, b, &hi);
    return hi ^ a * b;
}
#else
static inline uint64_t wy_mix(uint64_t a, uint64_t b) {
    uint64_t a_hi = a >> 32, a_lo = a & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32, b_lo = b & 0xFFFFFFFF;
    uint64_t r0 = a_lo * b_lo;
    uint64_t r1 = a_hi * b_lo + (r0 >> 32);
    uint64_t r2 = a_lo * b_hi + (r1 & 0xFFFFFFFF);
    uint64_t hi = a_hi * b_hi + (r1 >> 32) + (r2 >> 32);
    uint64_t lo = (r2 << 32) | (r0 & 0xFFFFFFFF);
    return hi ^ lo;
}
#endif

static uint64_t wy_next(void) {
    wyrand_global.state += WYRAND_INCREMENT;
    return wy_mix(wyrand_global.state, wyrand_global.state ^ WYRAND_MIX_CONST);
}

static int rnd_u64_secure(uint64_t *out) {
    unsigned char bytes[8];

    if (fill_random(bytes, sizeof(bytes)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to generate random bytes");
        return -1;
    }

    *out = unpack_u64_be(bytes);
    return 0;
}

int random_ensure_seeded(void) {
    unsigned char seed[16];
    uint64_t left = 0;
    uint64_t right = 0;

    if (wyrand_seeded) {
        return 0;
    }

    if (fill_random(seed, sizeof(seed)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to seed UUIDv7 generator");
        return -1;
    }

    memcpy(&left, seed, sizeof(left));
    memcpy(&right, seed + sizeof(left), sizeof(right));
    wyrand_global.state = left ^ wy_mix(right, right ^ WYRAND_MIX_CONST);
    wyrand_seeded = 1;
    return 0;
}

void random_reseed(void) {
    wyrand_seeded = 0;
}

uint64_t random_counter42(void) {
    return wy_next() & COUNTER42_MASK;
}

int random_counter42_secure(uint64_t *counter) {
    uint64_t random64 = 0;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    *counter = random64 & COUNTER42_MASK;
    return 0;
}

void random_split_counter42(const uint64_t counter,
                            const uint32_t low32,
                            uint16_t *rand_a,
                            uint64_t *tail62) {
    *rand_a = (uint16_t)(counter >> 30);
    *tail62 = (counter & LOW30_MASK) << 32 | (uint64_t)low32;
}

void random_next_low32_and_increment(uint32_t *low32, uint64_t *increment) {
    const uint64_t random64 = wy_next();

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
    random_split_counter42(random_counter42(), (uint32_t)wy_next(), rand_a, tail62);
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
    return wy_next() & RAND_MASK62;
}

int random_tail62_secure(uint64_t *tail62) {
    if (rnd_u64_secure(tail62) != 0) {
        return -1;
    }

    *tail62 &= RAND_MASK62;
    return 0;
}
