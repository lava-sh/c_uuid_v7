#ifndef RANDOM_H
#define RANDOM_H

#include "platform.h"

#include <stdint.h>

extern uint64_t wyrand_state_global;

#define RANDOM_NEXT_U64_VALUE() (wyrand_state_global += 0xA0761D6478BD642FULL)

static uint64_t random_counter42_direct(void) {
    const uint64_t value = RANDOM_NEXT_U64_VALUE();

#if defined(__SIZEOF_INT128__)
    const __uint128_t product = (__uint128_t)value * (value ^ 0xE7037ED1A0B428DBULL);
    return ((uint64_t)product ^ (uint64_t)(product >> 64)) & ((1ULL << 41) - 1ULL);
#elif defined(_MSC_VER) && defined(_M_X64)
    uint64_t high = 0;
    const uint64_t low = _umul128(value, value ^ 0xE7037ED1A0B428DBULL, &high);
    return (low ^ high) & ((1ULL << 41) - 1ULL);
#else
    return prng_mix64(value, value ^ 0xE7037ED1A0B428DBULL) & ((1ULL << 41) - 1ULL);
#endif
}

static void random_next_low32_and_increment_direct(uint32_t *low32, uint64_t *increment) {
    const uint64_t value = RANDOM_NEXT_U64_VALUE();

#if defined(__SIZEOF_INT128__)
    const __uint128_t product = (__uint128_t)value * (value ^ 0xE7037ED1A0B428DBULL);
    const uint64_t random64 = (uint64_t)product ^ (uint64_t)(product >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    uint64_t high = 0;
    const uint64_t low = _umul128(value, value ^ 0xE7037ED1A0B428DBULL, &high);
    const uint64_t random64 = low ^ high;
#else
    const uint64_t random64 = prng_mix64(value, value ^ 0xE7037ED1A0B428DBULL);
#endif

    *low32 = (uint32_t)random64;
    *increment = 1U + (random64 >> 32 & 0x0FU);
}

#undef RANDOM_NEXT_U64_VALUE

int random_ensure_seeded(void);
void random_reseed(void);
uint64_t random_counter42(void);
int random_counter42_secure(uint64_t *counter);
void random_next_low32_and_increment(uint32_t *low32, uint64_t *increment);
int random_next_low32_and_increment_secure(uint32_t *low32, uint64_t *increment);
void random_payload(uint16_t *rand_a, uint64_t *tail62);
int random_payload_secure(uint16_t *rand_a, uint64_t *tail62);
uint64_t random_tail62(void);
int random_tail62_secure(uint64_t *tail62);
void random_split_counter42(uint64_t counter, uint32_t low32, uint16_t *rand_a, uint64_t *tail62);

#endif
