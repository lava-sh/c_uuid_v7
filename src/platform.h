#ifndef PLATFORM_H
#define PLATFORM_H

#include <Python.h>

#include <stdint.h>

#define PY_3_11 0x030B0000

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #if !defined(_WIN32_WINNT)
        #if PY_VERSION_HEX >= PY_3_11
            #define _WIN32_WINNT 0x0A00
        #else
            #define _WIN32_WINNT 0x0603
        #endif
    #endif

    #include <windows.h>

    #if defined(_MSC_VER) && defined(_M_X64)
        #include <intrin.h>
        #pragma intrinsic(_umul128)
    #endif
#endif

[[nodiscard]]
uint64_t now_ms(void);
[[nodiscard]]
int fill_random(unsigned char *buf, Py_ssize_t len);

#endif
