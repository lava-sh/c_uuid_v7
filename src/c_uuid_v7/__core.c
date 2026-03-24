#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

/* clang-format off */
#include <windows.h>
#include <bcrypt.h>
/* clang-format on */

#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

typedef struct {
    PyObject_HEAD uint64_t hi;
    uint64_t lo;
} UUIDObject;

static PyTypeObject UUIDType;
static UUIDObject* uuid_cache = NULL;
static PyNumberMethods uuid_as_number;

static uint64_t rng_state0 = 0;
static uint64_t rng_state1 = 0;
static uint64_t last_timestamp_ms = 0;
static uint64_t counter42 = 0;
static int generator_seeded = 0;

#define UUID_TIMESTAMP_SHIFT 16
#define UUID_VERSION_BITS 0x7000ULL
#define UUID_VARIANT_BITS 0x8000000000000000ULL
#define UUID_RAND_MASK 0x3FFFFFFFFFFFFFFFULL
#define UUID_MAX_TIMESTAMP_MS 0xFFFFFFFFFFFFULL
#define UUID_MAX_TIMESTAMP_S (UUID_MAX_TIMESTAMP_MS / 1000ULL)
#define UUID_MAX_NANOS 1000000000ULL
#define UUID_V7_RESEED_MASK ((1ULL << 41) - 1ULL)
#define UUID_V7_MAX_COUNTER ((1ULL << 42) - 1ULL)
#define UUID_V7_LOW30_MASK ((1ULL << 30) - 1ULL)
#define UUID_MODE_FAST 0
#define UUID_MODE_SECURE 1
#ifdef _WIN32
static uint64_t epoch_base_ms = 0;
static uint64_t tick_base_ms = 0;
#endif

static inline UUIDObject*
uuid_self(PyObject* self_obj) {
    return (UUIDObject*)self_obj;
}

static inline const UUIDObject*
uuid_self_const(PyObject* self_obj) {
    return (const UUIDObject*)self_obj;
}

static uint64_t
uuid7_system_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimeAsFileTime(&ft);
    ticks.QuadPart = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    return (ticks.QuadPart - 116444736000000000ULL) / 10000ULL;
#elif defined(CLOCK_REALTIME)
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
    }
#endif

#ifndef _WIN32
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
#endif
}

#ifdef _WIN32
static inline uint64_t
uuid7_now_ms(void) {
    return epoch_base_ms + (GetTickCount64() - tick_base_ms);
}
#else
static inline uint64_t
uuid7_now_ms(void) {
    return uuid7_system_ms();
}
#endif

static int
fill_random(unsigned char* buf, Py_ssize_t len) {
#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status >= 0 ? 0 : -1;
#else
    Py_ssize_t offset = 0;

#if defined(__linux__)
    while (offset < len) {
        ssize_t rc = getrandom(buf + offset, (size_t)(len - offset), 0);
        if (rc < 0) {
            break;
        }
        offset += (Py_ssize_t)rc;
    }
    if (offset == len) {
        return 0;
    }
#endif

    int fd = open("/dev/urandom", O_RDONLY);
    offset = 0;

    if (fd < 0) {
        return -1;
    }

    while (offset < len) {
        ssize_t rc = read(fd, buf + offset, (size_t)(len - offset));
        if (rc <= 0) {
            close(fd);
            return -1;
        }
        offset += (Py_ssize_t)rc;
    }

    close(fd);
    return 0;
#endif
}

static uint64_t
rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static int
seed_generator(void) {
    unsigned char seed[16];

    if (generator_seeded) {
        return 0;
    }

    if (fill_random(seed, (Py_ssize_t)sizeof(seed)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to seed UUIDv7 generator");
        return -1;
    }

    memcpy(&rng_state0, seed, 8);
    memcpy(&rng_state1, seed + 8, 8);
    if ((rng_state0 | rng_state1) == 0) {
        rng_state1 = 0x9e3779b97f4a7c15ULL;
    }

#ifdef _WIN32
    epoch_base_ms = uuid7_system_ms();
    tick_base_ms = GetTickCount64();
#endif

    last_timestamp_ms = 0;
    counter42 = 0;
    generator_seeded = 1;
    return 0;
}

static void
reseed_generator_state(void) {
    generator_seeded = 0;
}

static int
ensure_seeded(void) {
    if (!generator_seeded && seed_generator() != 0) {
        return -1;
    }

    return 0;
}

static inline uint64_t
next_u64(void) {
    uint64_t s0 = rng_state0;
    uint64_t s1 = rng_state1;
    uint64_t result = s0 + s1;

    s1 ^= s0;
    rng_state0 = rotl64(s0, 55) ^ s1 ^ (s1 << 14);
    rng_state1 = rotl64(s1, 36);
    return result;
}

static inline uint64_t
unpack_u64_be(const unsigned char bytes[8]) {
    return ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) | ((uint64_t)bytes[2] << 40) |
           ((uint64_t)bytes[3] << 32) | ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
           ((uint64_t)bytes[6] << 8) | (uint64_t)bytes[7];
}

static int
rnd_u64_secure(uint64_t* out) {
    unsigned char bytes[8];

    if (fill_random(bytes, (Py_ssize_t)sizeof(bytes)) != 0) {
        PyErr_SetString(PyExc_OSError, "unable to generate random bytes");
        return -1;
    }

    *out = unpack_u64_be(bytes);
    return 0;
}

static inline uint64_t
rnd_counter42(void) {
    return next_u64() & UUID_V7_RESEED_MASK;
}

static int
rnd_counter42_secure(uint64_t* counter) {
    uint64_t random64;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    *counter = random64 & UUID_V7_RESEED_MASK;
    return 0;
}

static inline void
split_counter_random32(uint64_t counter, uint32_t low32, uint16_t* rand_a, uint64_t* tail62) {
    *rand_a = (uint16_t)(counter >> 30);
    *tail62 = ((counter & UUID_V7_LOW30_MASK) << 32) | (uint64_t)low32;
}

static inline void
next_low32_and_increment(uint32_t* low32, uint64_t* increment) {
    uint64_t random64 = next_u64();

    *low32 = (uint32_t)random64;
    *increment = 1U + ((random64 >> 32) & 0x0FU);
}

static int
next_low32_and_increment_secure(uint32_t* low32, uint64_t* increment) {
    uint64_t random64;

    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    *low32 = (uint32_t)random64;
    *increment = 1U + ((random64 >> 32) & 0x0FU);
    return 0;
}

static void
random_payload(uint16_t* rand_a, uint64_t* tail62) {
    split_counter_random32(rnd_counter42(), (uint32_t)next_u64(), rand_a, tail62);
}

static int
random_payload_secure(uint16_t* rand_a, uint64_t* tail62) {
    uint64_t counter;
    uint64_t random64;

    if (rnd_counter42_secure(&counter) != 0) {
        return -1;
    }
    if (rnd_u64_secure(&random64) != 0) {
        return -1;
    }

    split_counter_random32(counter, (uint32_t)random64, rand_a, tail62);
    return 0;
}

static inline uint64_t
random_tail62(void) {
    return next_u64() & UUID_RAND_MASK;
}

static int
random_tail62_secure(uint64_t* tail62) {
    if (rnd_u64_secure(tail62) != 0) {
        return -1;
    }

    *tail62 &= UUID_RAND_MASK;
    return 0;
}

static int
parse_u64_optional(PyObject* value, uint64_t* out, const char* name) {
    unsigned long long temp;

    if (value == NULL || value == Py_None) {
        return 0;
    }

    temp = PyLong_AsUnsignedLongLong(value);
    if (PyErr_Occurred()) {
        PyErr_Format(PyExc_TypeError, "%s must be a non-negative int or None", name);
        return -1;
    }

    *out = (uint64_t)temp;
    return 1;
}

typedef struct {
    uint64_t timestamp_ms;
    uint64_t nanos;
    int has_timestamp;
    int has_nanos;
} UUID7Args;

static int
validate_nanos(uint64_t nanos) {
    if (nanos >= UUID_MAX_NANOS) {
        PyErr_SetString(PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }
    return 0;
}

static int
build_timestamp_ms(uint64_t timestamp_s,
                   int has_timestamp,
                   uint64_t nanos,
                   int has_nanos,
                   uint64_t* timestamp_ms) {
    uint64_t ms = 0;

    if (!has_timestamp) {
        *timestamp_ms = uuid7_now_ms();
        return 0;
    }

    if (timestamp_s > UUID_MAX_TIMESTAMP_S) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    ms = timestamp_s * 1000ULL;
    if (has_nanos) {
        ms += nanos / 1000000ULL;
    }

    if (ms > UUID_MAX_TIMESTAMP_MS) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    *timestamp_ms = ms;
    return 0;
}

static int
parse_uuid7_args(PyObject* timestamp_obj, PyObject* nanos_obj, UUID7Args* parsed) {
    uint64_t timestamp_s = 0;

    parsed->nanos = 0;
    parsed->timestamp_ms = 0;

    parsed->has_timestamp = parse_u64_optional(timestamp_obj, &timestamp_s, "timestamp");
    if (parsed->has_timestamp < 0) {
        return -1;
    }

    parsed->has_nanos = parse_u64_optional(nanos_obj, &parsed->nanos, "nanos");
    if (parsed->has_nanos < 0) {
        return -1;
    }

    if (parsed->has_nanos && validate_nanos(parsed->nanos) != 0) {
        return -1;
    }

    return build_timestamp_ms(timestamp_s,
                              parsed->has_timestamp,
                              parsed->nanos,
                              parsed->has_nanos,
                              &parsed->timestamp_ms);
}

static void
advance_monotonic_state(uint64_t observed_ms,
                        uint64_t* timestamp_ms,
                        uint16_t* rand_a,
                        uint64_t* tail62) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment;
    uint32_t low32;

    next_low32_and_increment(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = rnd_counter42();
    } else {
        counter += increment;
        if (counter > UUID_V7_MAX_COUNTER) {
            current_ms += 1U;
            counter = rnd_counter42();
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    *timestamp_ms = current_ms;
    split_counter_random32(counter, low32, rand_a, tail62);
}

static int
advance_monotonic_state_secure(uint64_t observed_ms,
                               uint64_t* timestamp_ms,
                               uint16_t* rand_a,
                               uint64_t* tail62) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment;
    uint32_t low32;

    if (next_low32_and_increment_secure(&low32, &increment) != 0) {
        return -1;
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (rnd_counter42_secure(&counter) != 0) {
            return -1;
        }
    } else {
        counter += increment;
        if (counter > UUID_V7_MAX_COUNTER) {
            current_ms += 1U;
            if (rnd_counter42_secure(&counter) != 0) {
                return -1;
            }
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    *timestamp_ms = current_ms;
    split_counter_random32(counter, low32, rand_a, tail62);
    return 0;
}

static inline void
uuid_pack_bytes(uint64_t hi, uint64_t lo, unsigned char bytes[16]) {
    int i;

    for (i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)(hi >> (56 - (i * 8)));
        bytes[i + 8] = (unsigned char)(lo >> (56 - (i * 8)));
    }
}

static inline void
uuid_build_words(uint64_t timestamp_ms,
                 uint16_t rand_a,
                 uint64_t tail62,
                 uint64_t* hi,
                 uint64_t* lo) {
    *hi = (timestamp_ms << UUID_TIMESTAMP_SHIFT) | UUID_VERSION_BITS | (uint64_t)rand_a;
    *lo = UUID_VARIANT_BITS | tail62;
}

#define UUID_ULONG_GETTER(name, expr)                                                              \
    static PyObject* name(PyObject* self_obj, void* closure) {                                     \
        const UUIDObject* self = uuid_self_const(self_obj);                                        \
        (void)closure;                                                                             \
        return PyLong_FromUnsignedLong((unsigned long)(expr));                                     \
    }

#define UUID_ULL_GETTER(name, expr)                                                                \
    static PyObject* name(PyObject* self_obj, void* closure) {                                     \
        const UUIDObject* self = uuid_self_const(self_obj);                                        \
        (void)closure;                                                                             \
        return PyLong_FromUnsignedLongLong((unsigned long long)(expr));                            \
    }

static int
build_uuid7_default(uint64_t* hi, uint64_t* lo) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (ensure_seeded() != 0) {
        return -1;
    }

    advance_monotonic_state(uuid7_now_ms(), &timestamp_ms, &rand_a, &tail62);
    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
build_uuid7_default_secure(uint64_t* hi, uint64_t* lo) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (ensure_seeded() != 0) {
        return -1;
    }

    if (advance_monotonic_state_secure(uuid7_now_ms(), &timestamp_ms, &rand_a, &tail62) != 0) {
        return -1;
    }
    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
build_uuid7_with_parsed_args(const UUID7Args* args, uint64_t* hi, uint64_t* lo) {
    uint64_t tail62;
    uint16_t rand_a;

    if (ensure_seeded() != 0) {
        return -1;
    }

    if (args->has_timestamp && args->has_nanos) {
        rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        tail62 = random_tail62();
    } else if (args->has_timestamp || args->has_nanos) {
        random_payload(&rand_a, &tail62);
    } else {
        uint64_t timestamp_ms = args->timestamp_ms;

        advance_monotonic_state(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
fill_uuid7_random_bits(const UUID7Args* args, uint16_t* rand_a, uint64_t* tail62) {
    if (args->has_timestamp && args->has_nanos) {
        *rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        *tail62 = random_tail62();
        return 0;
    }

    if (args->has_timestamp || args->has_nanos) {
        random_payload(rand_a, tail62);
        return 0;
    }

    return 1;
}

static int
fill_uuid7_random_bits_secure(const UUID7Args* args, uint16_t* rand_a, uint64_t* tail62) {
    if (args->has_timestamp && args->has_nanos) {
        *rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        return random_tail62_secure(tail62);
    }

    if (args->has_timestamp || args->has_nanos) {
        return random_payload_secure(rand_a, tail62);
    }

    return 1;
}

static int
build_uuid7_with_parsed_args_secure(const UUID7Args* args, uint64_t* hi, uint64_t* lo) {
    uint64_t tail62;
    uint16_t rand_a;
    int state;

    state = fill_uuid7_random_bits_secure(args, &rand_a, &tail62);
    if (state < 0) {
        return -1;
    }
    if (state > 0) {
        uint64_t timestamp_ms = args->timestamp_ms;

        if (advance_monotonic_state_secure(timestamp_ms, &timestamp_ms, &rand_a, &tail62) != 0) {
            return -1;
        }
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
build_uuid7_parts_from_args(PyObject* timestamp_obj,
                            PyObject* nanos_obj,
                            int mode,
                            uint64_t* hi,
                            uint64_t* lo) {
    UUID7Args parsed;

    if (ensure_seeded() != 0) {
        return -1;
    }

    if (parse_uuid7_args(timestamp_obj, nanos_obj, &parsed) != 0) {
        return -1;
    }

    if (mode == UUID_MODE_SECURE) {
        return build_uuid7_with_parsed_args_secure(&parsed, hi, lo);
    }

    return build_uuid7_with_parsed_args(&parsed, hi, lo);
}

static int
parse_mode(PyObject* value, int* mode) {
    if (value == NULL || value == Py_None) {
        *mode = UUID_MODE_FAST;
        return 0;
    }

    if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "mode must be 'fast', 'secure', or None");
        return -1;
    }

    if (PyUnicode_CompareWithASCIIString(value, "fast") == 0) {
        *mode = UUID_MODE_FAST;
        return 0;
    }

    if (PyUnicode_CompareWithASCIIString(value, "secure") == 0) {
        *mode = UUID_MODE_SECURE;
        return 0;
    }

    PyErr_SetString(PyExc_ValueError, "mode must be 'fast' or 'secure'");
    return -1;
}

static inline UUIDObject*
uuid_new(uint64_t hi, uint64_t lo) {
    UUIDObject* obj;

    if (uuid_cache != NULL && Py_REFCNT(uuid_cache) == 1) {
        Py_INCREF(uuid_cache);
        uuid_cache->hi = hi;
        uuid_cache->lo = lo;
        return uuid_cache;
    }

    obj = PyObject_New(UUIDObject, &UUIDType);
    if (obj == NULL) {
        return NULL;
    }

    obj->hi = hi;
    obj->lo = lo;

    if (uuid_cache == NULL) {
        uuid_cache = obj;
        Py_INCREF(uuid_cache);
    }

    return obj;
}

static PyObject*
uuid_str(PyObject* self_obj) {
    static const char HEX[] = "0123456789abcdef";
    char out[36];
    unsigned char bytes[16];
    int i;
    int j = 0;
    const UUIDObject* self = uuid_self_const(self_obj);

    uuid_pack_bytes(self->hi, self->lo, bytes);

    for (i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[j++] = '-';
        }
        out[j++] = HEX[bytes[i] >> 4];
        out[j++] = HEX[bytes[i] & 0x0F];
    }

    return PyUnicode_FromStringAndSize(out, 36);
}

static PyObject*
uuid_repr(PyObject* self_obj) {
    PyObject* text = uuid_str(self_obj);
    PyObject* result;

    if (text == NULL) {
        return NULL;
    }

    result = PyUnicode_FromFormat("UUID('%U')", text);
    Py_DECREF(text);
    return result;
}

static PyObject*
uuid_hex(PyObject* self_obj, void* closure) {
    static const char HEX[] = "0123456789abcdef";
    char out[32];
    unsigned char bytes[16];
    int i;
    const UUIDObject* self = uuid_self_const(self_obj);

    uuid_pack_bytes(self->hi, self->lo, bytes);

    for (i = 0; i < 16; ++i) {
        out[i * 2] = HEX[bytes[i] >> 4];
        out[i * 2 + 1] = HEX[bytes[i] & 0x0F];
    }

    return PyUnicode_FromStringAndSize(out, 32);
}

static PyObject*
uuid_bytes(PyObject* self_obj, void* closure) {
    unsigned char bytes[16];
    const UUIDObject* self = uuid_self_const(self_obj);

    (void)closure;
    uuid_pack_bytes(self->hi, self->lo, bytes);
    return PyBytes_FromStringAndSize((const char*)bytes, 16);
}

static PyObject*
uuid_bytes_le(PyObject* self_obj, void* closure) {
    unsigned char bytes[16];
    unsigned char reordered[16];
    const UUIDObject* self = uuid_self_const(self_obj);

    (void)closure;
    uuid_pack_bytes(self->hi, self->lo, bytes);
    reordered[0] = bytes[3];
    reordered[1] = bytes[2];
    reordered[2] = bytes[1];
    reordered[3] = bytes[0];
    reordered[4] = bytes[5];
    reordered[5] = bytes[4];
    reordered[6] = bytes[7];
    reordered[7] = bytes[6];
    memcpy(reordered + 8, bytes + 8, 8);
    return PyBytes_FromStringAndSize((const char*)reordered, 16);
}

UUID_ULL_GETTER(uuid_timestamp, self->hi >> UUID_TIMESTAMP_SHIFT)

static PyObject*
uuid_int(PyObject* self_obj, void* closure) {
    const UUIDObject* self = uuid_self_const(self_obj);

#if PY_VERSION_HEX >= 0x030D0000
    unsigned char bytes[16];

    uuid_pack_bytes(self->hi, self->lo, bytes);

    return PyLong_FromUnsignedNativeBytes(
        bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
#else
    PyObject* high = PyLong_FromUnsignedLongLong(self->hi);
    PyObject* low = NULL;
    PyObject* bits = NULL;
    PyObject* shift = NULL;
    PyObject* value = NULL;

    if (high == NULL) {
        return NULL;
    }

    bits = PyLong_FromLong(64);
    if (bits == NULL) {
        Py_DECREF(high);
        return NULL;
    }

    shift = PyNumber_Lshift(high, bits);
    Py_DECREF(high);
    Py_DECREF(bits);
    if (shift == NULL) {
        return NULL;
    }

    low = PyLong_FromUnsignedLongLong(self->lo);
    if (low == NULL) {
        Py_DECREF(shift);
        return NULL;
    }

    value = PyNumber_Or(shift, low);
    Py_DECREF(shift);
    Py_DECREF(low);
    return value;
#endif
}

static PyObject*
uuid_nb_int(PyObject* self_obj) {
    return uuid_int(self_obj, NULL);
}

UUID_ULONG_GETTER(uuid_time_low, self->hi >> 32)
UUID_ULONG_GETTER(uuid_time_mid, (self->hi >> 16) & 0xFFFFULL)
UUID_ULONG_GETTER(uuid_time_hi_version, self->hi & 0xFFFFULL)
UUID_ULONG_GETTER(uuid_clock_seq_hi_variant, self->lo >> 56)
UUID_ULONG_GETTER(uuid_clock_seq_low, (self->lo >> 48) & 0xFFULL)

static PyObject*
uuid_clock_seq(PyObject* self_obj, void* closure) {
    unsigned long high;
    unsigned long low;
    const UUIDObject* self = uuid_self_const(self_obj);

    (void)closure;
    high = (unsigned long)((self->lo >> 56) & 0x3FULL);
    low = (unsigned long)((self->lo >> 48) & 0xFFULL);
    return PyLong_FromUnsignedLong((high << 8) | low);
}

UUID_ULL_GETTER(uuid_node, self->lo & 0xFFFFFFFFFFFFULL)

static PyObject*
uuid_fields(PyObject* self_obj, void* closure) {
    const UUIDObject* self = uuid_self_const(self_obj);

    (void)closure;
    return Py_BuildValue("(kkkkkK)",
                         (unsigned long)(self->hi >> 32),
                         (unsigned long)((self->hi >> 16) & 0xFFFFULL),
                         (unsigned long)(self->hi & 0xFFFFULL),
                         (unsigned long)(self->lo >> 56),
                         (unsigned long)((self->lo >> 48) & 0xFFULL),
                         (unsigned long long)(self->lo & 0xFFFFFFFFFFFFULL));
}

static PyObject*
uuid_urn(PyObject* self_obj, void* closure) {
    PyObject* text;
    PyObject* result;

    (void)closure;
    text = uuid_str(self_obj);
    if (text == NULL) {
        return NULL;
    }

    result = PyUnicode_FromFormat("urn:uuid:%U", text);
    Py_DECREF(text);
    return result;
}

static PyObject*
uuid_copy(PyObject* self_obj, PyObject* Py_UNUSED(args)) {
    Py_INCREF(self_obj);
    return self_obj;
}

static PyObject*
uuid_deepcopy(PyObject* self_obj, PyObject* args) {
    PyObject* memo;

    if (!PyArg_ParseTuple(args, "O:__deepcopy__", &memo)) {
        return NULL;
    }

    Py_INCREF(self_obj);
    return self_obj;
}

static Py_hash_t
uuid_hash(PyObject* self_obj) {
    const UUIDObject* self = uuid_self_const(self_obj);
    Py_hash_t hash = (Py_hash_t)(self->hi ^ (self->hi >> 32) ^ self->lo ^ (self->lo >> 32));

    if (hash == -1) {
        hash = -2;
    }

    return hash;
}

static PyObject*
uuid_richcompare(PyObject* a, PyObject* b, int op) {
    const UUIDObject* ua;
    const UUIDObject* ub;
    int cmp;

    if (!PyObject_TypeCheck(a, &UUIDType) || !PyObject_TypeCheck(b, &UUIDType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    ua = (const UUIDObject*)a;
    ub = (const UUIDObject*)b;

    if (ua->hi < ub->hi) {
        cmp = -1;
    } else if (ua->hi > ub->hi) {
        cmp = 1;
    } else if (ua->lo < ub->lo) {
        cmp = -1;
    } else if (ua->lo > ub->lo) {
        cmp = 1;
    } else {
        cmp = 0;
    }

    switch (op) {
        case Py_LT:
            return PyBool_FromLong(cmp < 0);
        case Py_LE:
            return PyBool_FromLong(cmp <= 0);
        case Py_EQ:
            return PyBool_FromLong(cmp == 0);
        case Py_NE:
            return PyBool_FromLong(cmp != 0);
        case Py_GT:
            return PyBool_FromLong(cmp > 0);
        case Py_GE:
            return PyBool_FromLong(cmp >= 0);
        default:
            Py_RETURN_NOTIMPLEMENTED;
    }
}

static PyMethodDef uuid_methods[] = {
    { "__copy__", (PyCFunction)uuid_copy, METH_NOARGS, "Return self for copy.copy()." },
    { "__deepcopy__",
      (PyCFunction)uuid_deepcopy,
      METH_VARARGS,
      "Return self for copy.deepcopy()." },
    { NULL, NULL, 0, NULL },
};

static PyGetSetDef uuid_getset[] = {
    { "bytes", (getter)uuid_bytes, NULL, "UUID as 16 big-endian bytes.", NULL },
    { "bytes_le", (getter)uuid_bytes_le, NULL, "UUID as 16 little-endian bytes.", NULL },
    { "clock_seq", (getter)uuid_clock_seq, NULL, "Clock sequence.", NULL },
    { "clock_seq_hi_variant",
      (getter)uuid_clock_seq_hi_variant,
      NULL,
      "Clock sequence high byte with variant.",
      NULL },
    { "clock_seq_low", (getter)uuid_clock_seq_low, NULL, "Clock sequence low byte.", NULL },
    { "fields", (getter)uuid_fields, NULL, "UUID fields tuple.", NULL },
    { "hex", (getter)uuid_hex, NULL, "Hexadecimal string.", NULL },
    { "int", (getter)uuid_int, NULL, "128-bit integer value.", NULL },
    { "node", (getter)uuid_node, NULL, "Node value.", NULL },
    { "time", (getter)uuid_timestamp, NULL, "UUID time value.", NULL },
    { "time_hi_version",
      (getter)uuid_time_hi_version,
      NULL,
      "Time high field with version bits.",
      NULL },
    { "time_low", (getter)uuid_time_low, NULL, "Time low field.", NULL },
    { "time_mid", (getter)uuid_time_mid, NULL, "Time middle field.", NULL },
    { "timestamp", (getter)uuid_timestamp, NULL, "Unix timestamp in milliseconds.", NULL },
    { "urn", (getter)uuid_urn, NULL, "UUID URN string.", NULL },
    { NULL, NULL, NULL, NULL, NULL },
};

static PyTypeObject UUIDType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "c_uuid_v7.UUID",
    .tp_basicsize = sizeof(UUIDObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_repr = (reprfunc)uuid_repr,
    .tp_str = (reprfunc)uuid_str,
    .tp_hash = (hashfunc)uuid_hash,
    .tp_richcompare = uuid_richcompare,
    .tp_methods = uuid_methods,
    .tp_getset = uuid_getset,
    .tp_as_number = &uuid_as_number,
};

static PyObject*
py_uuid7(PyObject* self, PyObject* const* args, Py_ssize_t nargs, PyObject* kwnames) {
    PyObject* timestamp_obj = Py_None;
    PyObject* nanos_obj = Py_None;
    PyObject* mode_obj = Py_None;
    Py_ssize_t nkw = kwnames == NULL ? 0 : PyTuple_GET_SIZE(kwnames);
    Py_ssize_t i;
    uint64_t hi;
    uint64_t lo;
    int mode = UUID_MODE_FAST;

    if (nargs == 0 && nkw == 0) {
        if (build_uuid7_default(&hi, &lo) != 0) {
            return NULL;
        }
        return (PyObject*)uuid_new(hi, lo);
    }

    if (nargs > 3) {
        PyErr_SetString(PyExc_TypeError, "uuid7() takes at most 3 positional arguments");
        return NULL;
    }

    if (nargs >= 1) {
        timestamp_obj = args[0];
    }
    if (nargs >= 2) {
        nanos_obj = args[1];
    }
    if (nargs >= 3) {
        mode_obj = args[2];
    }
    for (i = 0; i < nkw; ++i) {
        PyObject* key = PyTuple_GET_ITEM(kwnames, i);
        PyObject* value = args[nargs + i];

        if (PyUnicode_CompareWithASCIIString(key, "timestamp") == 0) {
            timestamp_obj = value;
        } else if (PyUnicode_CompareWithASCIIString(key, "nanos") == 0) {
            nanos_obj = value;
        } else if (PyUnicode_CompareWithASCIIString(key, "mode") == 0) {
            mode_obj = value;
        } else {
            PyErr_Format(PyExc_TypeError, "uuid7() got an unexpected keyword argument '%U'", key);
            return NULL;
        }
    }

    if (parse_mode(mode_obj, &mode) != 0) {
        return NULL;
    }

    if (mode == UUID_MODE_SECURE && timestamp_obj == Py_None && nanos_obj == Py_None) {
        if (build_uuid7_default_secure(&hi, &lo) != 0) {
            return NULL;
        }
    } else if (build_uuid7_parts_from_args(timestamp_obj, nanos_obj, mode, &hi, &lo) != 0) {
        return NULL;
    }

    return (PyObject*)uuid_new(hi, lo);
}

static PyObject*
py_reseed_rng(PyObject* self, PyObject* Py_UNUSED(args)) {
    (void)self;
    reseed_generator_state();
    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    { "_uuid7",
      (PyCFunction)(void (*)(void))py_uuid7,
      METH_FASTCALL | METH_KEYWORDS,
      "Generate a fast UUIDv7 object." },
    { "_reseed_rng", py_reseed_rng, METH_NOARGS, "Reseed the internal RNG state." },
    { NULL, NULL, 0, NULL },
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT, "__core", "Fast UUIDv7 generator.", -1, module_methods,
};

PyMODINIT_FUNC
PyInit___core(void) {
    PyObject* module;

    memset(&uuid_as_number, 0, sizeof(uuid_as_number));
    uuid_as_number.nb_int = (unaryfunc)uuid_nb_int;

    if (PyType_Ready(&UUIDType) < 0) {
        return NULL;
    }

    module = PyModule_Create(&module_def);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&UUIDType);
    if (PyModule_AddObject(module, "_UUID", (PyObject*)&UUIDType) < 0) {
        Py_DECREF(&UUIDType);
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
