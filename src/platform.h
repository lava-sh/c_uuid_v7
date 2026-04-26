#ifndef PLATFORM_H
#define PLATFORM_H

#include <Python.h>

#include <stdint.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #include <windows.h>

    #if defined(_MSC_VER) && defined(_M_X64)
        #include <intrin.h>
        #pragma intrinsic(_umul128)
    #endif
#endif

[[nodiscard]] uint64_t now_ms(void);
[[nodiscard]] int fill_random(unsigned char *buf, Py_ssize_t len);

#endif
