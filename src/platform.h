#ifndef PLATFORM_H
#define PLATFORM_H

#include <Python.h>

#include <stdint.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #include <realtimeapiset.h>
    #include <windows.h>

    #if defined(_M_X64)
        #include <intrin.h>
        #pragma intrinsic(_umul128)
    #endif

extern uint64_t epoch_base_ms;
extern uint64_t tick_base_ms;
#endif

uint64_t system_ms(void);
int fill_random(unsigned char *buf, Py_ssize_t len);
#ifdef _WIN32
void platform_seeded(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define NOT_UNUSED __attribute__((unused))
#else
    #define NOT_UNUSED
#endif

static NOT_UNUSED uint64_t now_ms(void) {
#ifdef _WIN32
    ULONGLONG interrupt_time = 0;

    QueryInterruptTime(&interrupt_time);
    return epoch_base_ms + interrupt_time / 10000ULL - tick_base_ms;
#else
    return system_ms();
#endif
}

#undef NOT_UNUSED

#endif
