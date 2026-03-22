#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
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
    PyObject_HEAD
    uint64_t hi;
    uint64_t lo;
} UUIDObject;

static PyTypeObject UUIDType;
static UUIDObject *uuid_cache = NULL;

static uint64_t rng_state0 = 0;
static uint64_t rng_state1 = 0;
static uint64_t last_timestamp_ms = 0;
static uint64_t rand_tail = 0;
static uint16_t sequence12 = 0;
static int generator_seeded = 0;

#define UUID_TIMESTAMP_SHIFT 16
#define UUID_VERSION_BITS 0x7000ULL
#define UUID_VARIANT_BITS 0x8000000000000000ULL
#define UUID_RAND_MASK 0x3FFFFFFFFFFFFFFFULL
#define UUID_MAX_TIMESTAMP_MS 0xFFFFFFFFFFFFULL
#define UUID_MAX_TIMESTAMP_S (UUID_MAX_TIMESTAMP_MS / 1000ULL)
#define UUID_MAX_NANOS 1000000000ULL
#ifdef _WIN32
static uint64_t epoch_base_ms = 0;
static uint64_t tick_base_ms = 0;
#endif

static uint64_t
uuid7_system_ms(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimeAsFileTime(&ft);
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
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
uuid7_now_ms(void)
{
    return epoch_base_ms + (GetTickCount64() - tick_base_ms);
}
#else
static inline uint64_t
uuid7_now_ms(void)
{
    return uuid7_system_ms();
}
#endif

static int
fill_random(unsigned char *buf, Py_ssize_t len)
{
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
rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static int
seed_generator(void)
{
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
    rand_tail = 0;
    sequence12 = 0;
    generator_seeded = 1;
    return 0;
}

static void
reseed_generator_state(void)
{
    generator_seeded = 0;
}

static inline uint64_t
next_u64(void)
{
    uint64_t s0 = rng_state0;
    uint64_t s1 = rng_state1;
    uint64_t result = s0 + s1;

    s1 ^= s0;
    rng_state0 = rotl64(s0, 55) ^ s1 ^ (s1 << 14);
    rng_state1 = rotl64(s1, 36);
    return result;
}

static int
parse_u64_optional(PyObject *value, uint64_t *out, const char *name)
{
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

static int
validate_nanos(uint64_t nanos)
{
    if (nanos >= UUID_MAX_NANOS) {
        PyErr_SetString(PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }
    return 0;
}

static int
build_timestamp_ms(uint64_t timestamp_s, int has_timestamp, uint64_t nanos, int has_nanos, uint64_t *timestamp_ms)
{
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

static inline void
advance_monotonic_state(uint64_t timestamp_ms, uint16_t *rand_a, uint64_t *tail62)
{
    if (timestamp_ms > last_timestamp_ms) {
        last_timestamp_ms = timestamp_ms;
        sequence12 = (uint16_t)(next_u64() & 0x0FFFU);
        rand_tail = next_u64() & UUID_RAND_MASK;
    } else {
        last_timestamp_ms += (timestamp_ms < last_timestamp_ms);
        sequence12 = (uint16_t)((sequence12 + 1U) & 0x0FFFU);
        rand_tail = (rand_tail + 1U) & UUID_RAND_MASK;
    }

    *rand_a = sequence12;
    *tail62 = rand_tail;
}

static inline void
uuid_pack_bytes(uint64_t hi, uint64_t lo, unsigned char bytes[16])
{
    int i;

    for (i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)(hi >> (56 - (i * 8)));
        bytes[i + 8] = (unsigned char)(lo >> (56 - (i * 8)));
    }
}

static int
build_uuid7_default(uint64_t *hi, uint64_t *lo)
{
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (seed_generator() != 0) {
        return -1;
    }

    timestamp_ms = uuid7_now_ms();
    advance_monotonic_state(timestamp_ms, &rand_a, &tail62);
    timestamp_ms = last_timestamp_ms;

    *hi = (timestamp_ms << UUID_TIMESTAMP_SHIFT) | UUID_VERSION_BITS | (uint64_t)rand_a;
    *lo = UUID_VARIANT_BITS | tail62;
    return 0;
}

static int
build_uuid7_parts_from_args(PyObject *timestamp_obj, PyObject *nanos_obj, uint64_t *hi, uint64_t *lo)
{
    uint64_t timestamp_s = 0;
    uint64_t timestamp_ms = 0;
    uint64_t nanos = 0;
    uint64_t tail62;
    uint16_t rand_a;
    int has_timestamp;
    int has_nanos;

    if (seed_generator() != 0) {
        return -1;
    }

    has_timestamp = parse_u64_optional(timestamp_obj, &timestamp_s, "timestamp");
    if (has_timestamp < 0) {
        return -1;
    }

    has_nanos = parse_u64_optional(nanos_obj, &nanos, "nanos");
    if (has_nanos < 0) {
        return -1;
    }

    if (has_nanos && validate_nanos(nanos) != 0) {
        return -1;
    }

    if (build_timestamp_ms(timestamp_s, has_timestamp, nanos, has_nanos, &timestamp_ms) != 0) {
        return -1;
    }

    if (has_timestamp) {
        rand_a = has_nanos ? (uint16_t)(nanos & 0x0FFFU) : (uint16_t)(next_u64() & 0x0FFFU);
        tail62 = next_u64() & UUID_RAND_MASK;
    } else if (has_nanos) {
        rand_a = (uint16_t)(nanos & 0x0FFFU);
        tail62 = next_u64() & UUID_RAND_MASK;
    } else {
        advance_monotonic_state(timestamp_ms, &rand_a, &tail62);
        timestamp_ms = last_timestamp_ms;
    }

    *hi = (timestamp_ms << UUID_TIMESTAMP_SHIFT) | UUID_VERSION_BITS | (uint64_t)rand_a;
    *lo = UUID_VARIANT_BITS | tail62;
    return 0;
}

static inline UUIDObject *
uuid_new_fast(uint64_t hi, uint64_t lo)
{
    UUIDObject *obj;

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

static PyObject *
uuid_str(UUIDObject *self)
{
    static const char HEX[] = "0123456789abcdef";
    char out[36];
    unsigned char bytes[16];
    int i;
    int j = 0;

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

static PyObject *
uuid_repr(UUIDObject *self)
{
    PyObject *text = uuid_str(self);
    PyObject *result;

    if (text == NULL) {
        return NULL;
    }

    result = PyUnicode_FromFormat("UUID('%U')", text);
    Py_DECREF(text);
    return result;
}

static PyObject *
uuid_hex(UUIDObject *self, void *closure)
{
    static const char HEX[] = "0123456789abcdef";
    char out[32];
    unsigned char bytes[16];
    int i;

    uuid_pack_bytes(self->hi, self->lo, bytes);

    for (i = 0; i < 16; ++i) {
        out[i * 2] = HEX[bytes[i] >> 4];
        out[i * 2 + 1] = HEX[bytes[i] & 0x0F];
    }

    return PyUnicode_FromStringAndSize(out, 32);
}

static PyObject *
uuid_version(UUIDObject *self, void *closure)
{
    return PyLong_FromLong(7);
}

static PyObject *
uuid_timestamp(UUIDObject *self, void *closure)
{
    return PyLong_FromUnsignedLongLong(self->hi >> UUID_TIMESTAMP_SHIFT);
}

static PyObject *
uuid_int(UUIDObject *self, void *closure)
{
    unsigned char bytes[16];

    uuid_pack_bytes(self->hi, self->lo, bytes);

    return PyLong_FromUnsignedNativeBytes(
        bytes,
        16,
        Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER
    );
}

static Py_hash_t
uuid_hash(UUIDObject *self)
{
    return (Py_hash_t)(self->hi ^ (self->hi >> 32) ^ self->lo ^ (self->lo >> 32));
}

static PyObject *
uuid_richcompare(PyObject *a, PyObject *b, int op)
{
    UUIDObject *ua;
    UUIDObject *ub;
    int cmp;

    if (!PyObject_TypeCheck(a, &UUIDType) || !PyObject_TypeCheck(b, &UUIDType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    ua = (UUIDObject *)a;
    ub = (UUIDObject *)b;

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
        case Py_LT: return PyBool_FromLong(cmp < 0);
        case Py_LE: return PyBool_FromLong(cmp <= 0);
        case Py_EQ: return PyBool_FromLong(cmp == 0);
        case Py_NE: return PyBool_FromLong(cmp != 0);
        case Py_GT: return PyBool_FromLong(cmp > 0);
        case Py_GE: return PyBool_FromLong(cmp >= 0);
        default: Py_RETURN_NOTIMPLEMENTED;
    }
}

static PyGetSetDef uuid_getset[] = {
    {"hex", (getter)uuid_hex, NULL, "Hexadecimal string.", NULL},
    {"int", (getter)uuid_int, NULL, "128-bit integer value.", NULL},
    {"timestamp", (getter)uuid_timestamp, NULL, "Unix timestamp in milliseconds.", NULL},
    {"version", (getter)uuid_version, NULL, "UUID version.", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyTypeObject UUIDType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "c_uuid_v7.UUID",
    .tp_basicsize = sizeof(UUIDObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_repr = (reprfunc)uuid_repr,
    .tp_str = (reprfunc)uuid_str,
    .tp_hash = (hashfunc)uuid_hash,
    .tp_richcompare = uuid_richcompare,
    .tp_getset = uuid_getset,
};

static PyObject *
py_uuid7(PyObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *timestamp_obj = Py_None;
    PyObject *nanos_obj = Py_None;
    Py_ssize_t nkw = kwnames == NULL ? 0 : PyTuple_GET_SIZE(kwnames);
    Py_ssize_t i;
    uint64_t hi;
    uint64_t lo;

    if (nargs == 0 && nkw == 0) {
        if (build_uuid7_default(&hi, &lo) != 0) {
            return NULL;
        }
        return (PyObject *)uuid_new_fast(hi, lo);
    }

    if (nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "uuid7() takes at most 2 positional arguments");
        return NULL;
    }

    if (nargs >= 1) {
        timestamp_obj = args[0];
    }
    if (nargs >= 2) {
        nanos_obj = args[1];
    }

    for (i = 0; i < nkw; ++i) {
        PyObject *key = PyTuple_GET_ITEM(kwnames, i);
        PyObject *value = args[nargs + i];

        if (PyUnicode_CompareWithASCIIString(key, "timestamp") == 0) {
            timestamp_obj = value;
        } else if (PyUnicode_CompareWithASCIIString(key, "nanos") == 0) {
            nanos_obj = value;
        } else {
            PyErr_Format(PyExc_TypeError, "uuid7() got an unexpected keyword argument '%U'", key);
            return NULL;
        }
    }

    if (build_uuid7_parts_from_args(timestamp_obj, nanos_obj, &hi, &lo) != 0) {
        return NULL;
    }

    return (PyObject *)uuid_new_fast(hi, lo);
}

static PyObject *
py_reseed_rng(PyObject *self, PyObject *Py_UNUSED(args))
{
    reseed_generator_state();
    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    {"_uuid7", (PyCFunction)(void (*)(void))py_uuid7, METH_FASTCALL | METH_KEYWORDS, "Generate a fast UUIDv7 object."},
    {"_reseed_rng", py_reseed_rng, METH_NOARGS, "Reseed the internal RNG state."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_uuid_v7_c",
    "Fast UUIDv7 generator.",
    -1,
    module_methods,
};

PyMODINIT_FUNC
PyInit__uuid_v7_c(void)
{
    PyObject *module;

    if (PyType_Ready(&UUIDType) < 0) {
        return NULL;
    }

    module = PyModule_Create(&module_def);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&UUIDType);
    if (PyModule_AddObject(module, "UUID", (PyObject *)&UUIDType) < 0) {
        Py_DECREF(&UUIDType);
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
