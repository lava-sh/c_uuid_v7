#ifndef PLATFORM_H
#define PLATFORM_H

#include <Python.h>

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

#include <intrin.h>
#include <windows.h>

#pragma intrinsic(_umul128)

extern uint64_t epoch_base_ms;
extern uint64_t tick_base_ms;
extern VOID(WINAPI *query_interrupt_time_ptr)(PULONGLONG);
#endif

uint64_t system_ms(void);
int fill_random(unsigned char *buf, Py_ssize_t len);
#ifdef _WIN32
void platform_seeded(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

static MAYBE_UNUSED uint64_t now_ms(void) {
#ifdef _WIN32
    if (query_interrupt_time_ptr != NULL) {
        ULONGLONG interrupt_time = 0;

        query_interrupt_time_ptr(&interrupt_time);
        return epoch_base_ms + interrupt_time / 10000ULL - tick_base_ms;
    }

    return epoch_base_ms + GetTickCount64() - tick_base_ms;
#else
    return system_ms();
#endif
}

static MAYBE_UNUSED uint64_t prng_mix64(const uint64_t left,
                                                         const uint64_t right) {
#if defined(__SIZEOF_INT128__)
    const __uint128_t product = (__uint128_t)left * right;

    return (uint64_t)product ^ (uint64_t)(product >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    uint64_t high = 0;
    const uint64_t low = _umul128(left, right, &high);

    return low ^ high;
#else
    const uint64_t left_hi = left >> 32;
    const uint64_t left_lo = (uint32_t)left;
    const uint64_t right_hi = right >> 32;
    const uint64_t right_lo = (uint32_t)right;
    const uint64_t rh = left_hi * right_hi;
    const uint64_t rm0 = left_hi * right_lo;
    const uint64_t rm1 = right_hi * left_lo;
    const uint64_t rl = left_lo * right_lo;
    const uint64_t t = rl + (rm0 << 32);
    const uint64_t carry = t < rl;
    const uint64_t low = t + (rm1 << 32);
    const uint64_t high = rh + (rm0 >> 32) + (rm1 >> 32) + carry + (low < t);

    return low ^ high;
#endif
}

#undef MAYBE_UNUSED

#endif
