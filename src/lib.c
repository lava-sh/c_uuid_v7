#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "hexpairs.h"
#include "platform.h"
#include "random.h"

#include <stdint.h>

typedef struct {
    PyObject_HEAD uint64_t hi;
    uint64_t lo;
} UUIDObject;

static PyTypeObject UUIDType;
static UUIDObject *uuid_cache = NULL;
static PyNumberMethods uuid_as_number;

static uint64_t last_timestamp_ms = 0;
static uint64_t counter42 = 0;

extern int wyrand_seeded;
extern uint64_t wyrand_state_global;

#define V7_TIMESTAMP_SHIFT 16
#define V7_VERSION_BITS 0x7000ULL
#define UUID_VARIANT_BITS 0x8000000000000000ULL
#define MAX_UUID_TIMESTAMP_MS 0xFFFFFFFFFFFFULL
#define MAX_UUID_TIMESTAMP_S (MAX_UUID_TIMESTAMP_MS / 1000ULL)
#define MAX_NANOS 1000000000ULL
#define V7_MAX_COUNTER ((1ULL << 42) - 1ULL)
#define MODE_FAST 0
#define MODE_SECURE 1

static const UUIDObject *uuid_self_const(const PyObject *self_obj) {
    return (const UUIDObject *)self_obj;
}

static int ensure_seeded(void) {
    if (random_ensure_seeded() != 0) {
        return -1;
    }

    return 0;
}

static int parse_u64(PyObject *value, uint64_t *out, const char *name) {
    if (value == NULL || value == Py_None) {
        return 0;
    }

    const unsigned long long temp = PyLong_AsUnsignedLongLong(value);
    if (PyErr_Occurred()) {
        PyErr_Format(PyExc_TypeError, "%s must be a non-negative int or None", name);
        return -1;
    }

    *out = temp;
    return 1;
}

typedef struct {
    uint64_t timestamp_ms;
    uint64_t nanos;
    int has_timestamp;
    int has_nanos;
} UUID7Args;

static int fill_random_bits(const UUID7Args *args, int mode, uint16_t *rand_a, uint64_t *tail62);
static int advance_monotonic_state(uint64_t observed_ms,
                                   int mode,
                                   uint64_t *timestamp_ms,
                                   uint16_t *rand_a,
                                   uint64_t *tail62);

static uint64_t random_counter42_direct(void) {
    const uint64_t value = wyrand_state_global += 0xA0761D6478BD642FULL;

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
    const uint64_t value = wyrand_state_global += 0xA0761D6478BD642FULL;

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

static int build_timestamp_ms(const uint64_t timestamp_s,
                              const int has_timestamp,
                              const uint64_t nanos,
                              const int has_nanos,
                              uint64_t *timestamp_ms) {
    uint64_t ms = 0;

    if (!has_timestamp) {
        *timestamp_ms = now_ms();
        return 0;
    }

    if (timestamp_s > MAX_UUID_TIMESTAMP_S) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    ms = timestamp_s * 1000ULL;
    if (has_nanos) {
        ms += nanos / 1000000ULL;
    }

    if (ms > MAX_UUID_TIMESTAMP_MS) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    *timestamp_ms = ms;
    return 0;
}

static int parse_args(PyObject *timestamp_obj, PyObject *nanos_obj, UUID7Args *parsed) {
    uint64_t timestamp_s = 0;

    parsed->nanos = 0;
    parsed->timestamp_ms = 0;

    parsed->has_timestamp = parse_u64(timestamp_obj, &timestamp_s, "timestamp");
    if (parsed->has_timestamp < 0) {
        return -1;
    }

    parsed->has_nanos = parse_u64(nanos_obj, &parsed->nanos, "nanos");
    if (parsed->has_nanos < 0) {
        return -1;
    }

    if (parsed->has_nanos && parsed->nanos >= MAX_NANOS) {
        PyErr_SetString(PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }

    return build_timestamp_ms(timestamp_s,
                              parsed->has_timestamp,
                              parsed->nanos,
                              parsed->has_nanos,
                              &parsed->timestamp_ms);
}

static UUIDObject *uuid_new_with_generated_default(void) {
    UUIDObject *obj = NULL;
    uint64_t increment = 0;
    uint32_t low32 = 0;
    uint16_t rand_a = 0;
    uint64_t tail62 = 0;

    if (!wyrand_seeded && random_ensure_seeded() != 0) {
        return NULL;
    }

    uint64_t current_ms = last_timestamp_ms;
    uint64_t counter = counter42;
    const uint64_t observed_ms = now_ms();

    if (uuid_cache != NULL && Py_REFCNT(uuid_cache) == 1) {
        Py_INCREF(uuid_cache);
        obj = uuid_cache;
    } else {
        obj = PyObject_New(UUIDObject, &UUIDType);
        if (obj == NULL) {
            return NULL;
        }

        if (uuid_cache == NULL) {
            uuid_cache = obj;
            Py_INCREF(uuid_cache);
        }
    }

    random_next_low32_and_increment_direct(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = random_counter42_direct();
    } else {
        counter += increment;
        if (counter > V7_MAX_COUNTER) {
            current_ms += 1U;
            counter = random_counter42_direct();
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    random_split_counter42(counter, low32, &rand_a, &tail62);
    obj->hi = current_ms << V7_TIMESTAMP_SHIFT | V7_VERSION_BITS | (uint64_t)rand_a;
    obj->lo = UUID_VARIANT_BITS | tail62;
    return obj;
}

static int advance_monotonic_state(const uint64_t observed_ms,
                                   const int mode,
                                   uint64_t *timestamp_ms,
                                   uint16_t *rand_a,
                                   uint64_t *tail62) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment;
    uint32_t low32;

    if (mode == MODE_SECURE) {
        if (random_next_low32_and_increment_secure(&low32, &increment) != 0) {
            return -1;
        }
    } else {
        random_next_low32_and_increment(&low32, &increment);
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (mode == MODE_SECURE) {
            if (random_counter42_secure(&counter) != 0) {
                return -1;
            }
        } else {
            counter = random_counter42();
        }
    } else {
        counter += increment;
        if (counter > V7_MAX_COUNTER) {
            current_ms += 1U;
            if (mode == MODE_SECURE) {
                if (random_counter42_secure(&counter) != 0) {
                    return -1;
                }
            } else {
                counter = random_counter42();
            }
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    *timestamp_ms = current_ms;
    random_split_counter42(counter, low32, rand_a, tail62);
    return 0;
}

static uint64_t byteswap_u64(const uint64_t value) {
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap64(value);
#else
    return _byteswap_uint64(value);
#endif
}

static void uuid_pack_bytes(const uint64_t hi, const uint64_t lo, unsigned char bytes[16]) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    memcpy(bytes, &hi, sizeof(hi));
    memcpy(bytes + 8, &lo, sizeof(lo));
#elif defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    const uint64_t hi_be = byteswap_u64(hi);
    const uint64_t lo_be = byteswap_u64(lo);

    memcpy(bytes, &hi_be, sizeof(hi_be));
    memcpy(bytes + 8, &lo_be, sizeof(lo_be));
#else
    for (int i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)(hi >> (56 - i * 8));
        bytes[i + 8] = (unsigned char)(lo >> (56 - i * 8));
    }
#endif
}

static void uuid_build_words(const uint64_t timestamp_ms,
                             const uint16_t rand_a,
                             const uint64_t tail62,
                             uint64_t *hi,
                             uint64_t *lo) {
    *hi = timestamp_ms << V7_TIMESTAMP_SHIFT | V7_VERSION_BITS | (uint64_t)rand_a;
    *lo = UUID_VARIANT_BITS | tail62;
}

static void uuid_write_hex32(const uint32_t value, char *out) {
    int j = 0;

    for (int shift = 24; shift >= 0; shift -= 8) {
        hex_pair(out + j, (unsigned char)(value >> shift));
        j += 2;
    }
}

static void uuid_write_hex16(const uint16_t value, char *out) {
    int j = 0;

    for (int shift = 8; shift >= 0; shift -= 8) {
        hex_pair(out + j, (unsigned char)(value >> shift));
        j += 2;
    }
}

static void uuid_write_hex48(const uint64_t value, char *out) {
    int j = 0;

    for (int shift = 40; shift >= 0; shift -= 8) {
        hex_pair(out + j, (unsigned char)(value >> shift));
        j += 2;
    }
}

static void uuid_write_hex64(const uint64_t value, char *out) {
    uuid_write_hex32((uint32_t)(value >> 32), out);
    uuid_write_hex32((uint32_t)value, out + 8);
}

static void uuid_write_str(const UUIDObject *self, char out[36]) {
    uuid_write_hex32((uint32_t)(self->hi >> 32), out);
    out[8] = '-';
    uuid_write_hex16((uint16_t)(self->hi >> 16), out + 9);
    out[13] = '-';
    uuid_write_hex16((uint16_t)self->hi, out + 14);
    out[18] = '-';
    uuid_write_hex16((uint16_t)(self->lo >> 48), out + 19);
    out[23] = '-';
    uuid_write_hex48(self->lo & 0xFFFFFFFFFFFFULL, out + 24);
}

static void uuid_write_hex(const UUIDObject *self, char out[32]) {
    uuid_write_hex64(self->hi, out);
    uuid_write_hex64(self->lo, out + 16);
}

#define UUID_ULONG_GETTER_SPECS(X)                                                                 \
    X(uuid_time_low, "time_low", self->hi >> 32, "Time low field.")                                \
    X(uuid_time_mid, "time_mid", self->hi >> 16 & 0xFFFFULL, "Time middle field.")                 \
    X(uuid_time_hi_version,                                                                        \
      "time_hi_version",                                                                           \
      self->hi & 0xFFFFULL,                                                                        \
      "Time high field with version bits.")                                                        \
    X(uuid_clock_seq_hi_variant,                                                                   \
      "clock_seq_hi_variant",                                                                      \
      self->lo >> 56,                                                                              \
      "Clock sequence high byte with variant.")                                                    \
    X(uuid_clock_seq_low, "clock_seq_low", self->lo >> 48 & 0xFFULL, "Clock sequence low byte.")

#define UUID_ULL_GETTER_SPECS(X) X(uuid_node, "node", self->lo & 0xFFFFFFFFFFFFULL, "Node value.")

#define DEFINE_UUID_ULONG_GETTER(name, property_name, expr, doc)                                   \
    /* ReSharper disable once CppParameterMayBeConstPtrOrRef */                                    \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                          \
        const UUIDObject *self = uuid_self_const(self_obj);                                        \
        return PyLong_FromUnsignedLong((unsigned long)(expr));                                     \
    }

#define DEFINE_UUID_ULL_GETTER(name, property_name, expr, doc)                                     \
    /* ReSharper disable once CppParameterMayBeConstPtrOrRef */                                    \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                          \
        const UUIDObject *self = uuid_self_const(self_obj);                                        \
        return PyLong_FromUnsignedLongLong((unsigned long long)(expr));                            \
    }

UUID_ULONG_GETTER_SPECS(DEFINE_UUID_ULONG_GETTER)
UUID_ULL_GETTER_SPECS(DEFINE_UUID_ULL_GETTER)
#undef DEFINE_UUID_ULONG_GETTER
#undef DEFINE_UUID_ULL_GETTER

static int build_uuid7_default(const int mode, uint64_t *hi, uint64_t *lo) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (ensure_seeded() != 0) {
        return -1;
    }

    if (advance_monotonic_state(now_ms(), mode, &timestamp_ms, &rand_a, &tail62) != 0) {
        return -1;
    }

    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
build_uuid7_with_parsed_args(const UUID7Args *args, const int mode, uint64_t *hi, uint64_t *lo) {
    uint64_t tail62;
    uint16_t rand_a;

    const int state = fill_random_bits(args, mode, &rand_a, &tail62);

    if (state < 0) {
        return -1;
    }

    if (state > 0) {
        uint64_t timestamp_ms = args->timestamp_ms;

        if (advance_monotonic_state(timestamp_ms, mode, &timestamp_ms, &rand_a, &tail62) != 0) {
            return -1;
        }
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
fill_random_bits(const UUID7Args *args, const int mode, uint16_t *rand_a, uint64_t *tail62) {
    if (args->has_timestamp && args->has_nanos) {
        *rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        if (mode == MODE_SECURE) {
            return random_tail62_secure(tail62);
        }

        *tail62 = random_tail62();
        return 0;
    }

    if (args->has_timestamp || args->has_nanos) {
        if (mode == MODE_SECURE) {
            return random_payload_secure(rand_a, tail62);
        }

        random_payload(rand_a, tail62);
        return 0;
    }

    return 1;
}

static int parse_mode(PyObject *value, int *mode) {
    if (value == NULL || value == Py_None) {
        *mode = MODE_FAST;
        return 0;
    }

    if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "mode must be 'fast', 'secure', or None");
        return -1;
    }

    if (PyUnicode_CompareWithASCIIString(value, "fast") == 0) {
        *mode = MODE_FAST;
        return 0;
    }

    if (PyUnicode_CompareWithASCIIString(value, "secure") == 0) {
        *mode = MODE_SECURE;
        return 0;
    }

    PyErr_SetString(PyExc_ValueError, "mode must be 'fast' or 'secure'");
    return -1;
}

static UUIDObject *uuid_new(const uint64_t hi, const uint64_t lo) {
    if (uuid_cache != NULL && Py_REFCNT(uuid_cache) == 1) {
        Py_INCREF(uuid_cache);
        uuid_cache->hi = hi;
        uuid_cache->lo = lo;
        return uuid_cache;
    }

    UUIDObject *obj = PyObject_New(UUIDObject, &UUIDType);
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

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_str(PyObject *self_obj) {
    char out[36];
    const UUIDObject *self = uuid_self_const(self_obj);

    uuid_write_str(self, out);

    return PyUnicode_FromStringAndSize(out, 36);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_repr(PyObject *self_obj) {
    char out[44];
    const UUIDObject *self = uuid_self_const(self_obj);

    memcpy(out, "UUID('", 6);
    uuid_write_str(self, out + 6);
    out[42] = '\'';
    out[43] = ')';
    return PyUnicode_FromStringAndSize(out, 44);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_hex(PyObject *self_obj, void *Py_UNUSED(closure)) {
    char out[32];
    const UUIDObject *self = uuid_self_const(self_obj);

    uuid_write_hex(self, out);

    return PyUnicode_FromStringAndSize(out, 32);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_bytes(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    const UUIDObject *self = uuid_self_const(self_obj);

    uuid_pack_bytes(self->hi, self->lo, bytes);
    // ReSharper disable once CppRedundantCastExpression
    return PyBytes_FromStringAndSize((const char *)bytes, 16);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_bytes_le(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    unsigned char reordered[16];
    const UUIDObject *self = uuid_self_const(self_obj);

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
    // ReSharper disable once CppRedundantCastExpression
    return PyBytes_FromStringAndSize((const char *)reordered, 16);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_timestamp(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = uuid_self_const(self_obj);
    return PyLong_FromUnsignedLongLong(self->hi >> V7_TIMESTAMP_SHIFT);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_int(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = uuid_self_const(self_obj);

#if PY_VERSION_HEX >= 0x030D0000
    unsigned char bytes[16];

    uuid_pack_bytes(self->hi, self->lo, bytes);

    return PyLong_FromUnsignedNativeBytes(
        bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
#else
    PyObject *high = PyLong_FromUnsignedLongLong(self->hi);
    PyObject *low = NULL;
    PyObject *bits = NULL;
    PyObject *shift = NULL;
    PyObject *value = NULL;

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

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_nb_int(PyObject *self_obj) {
    return uuid_int(self_obj, NULL);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_clock_seq(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = uuid_self_const(self_obj);
    const unsigned long high = (unsigned long)(self->lo >> 56 & 0x3FULL);
    const unsigned long low = (unsigned long)(self->lo >> 48 & 0xFFULL);
    return PyLong_FromUnsignedLong(high << 8 | low);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_fields(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = uuid_self_const(self_obj);
    return Py_BuildValue("(kkkkkK)",
                         (unsigned long)(self->hi >> 32),
                         (unsigned long)(self->hi >> 16 & 0xFFFFULL),
                         (unsigned long)(self->hi & 0xFFFFULL),
                         (unsigned long)(self->lo >> 56),
                         (unsigned long)(self->lo >> 48 & 0xFFULL),
                         (unsigned long long)(self->lo & 0xFFFFFFFFFFFFULL));
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_urn(PyObject *self_obj, void *Py_UNUSED(closure)) {
    char out[45];
    const UUIDObject *self = uuid_self_const(self_obj);

    memcpy(out, "urn:uuid:", 9);
    uuid_write_str(self, out + 9);
    return PyUnicode_FromStringAndSize(out, 45);
}

static PyObject *uuid_copy(PyObject *self_obj, PyObject *Py_UNUSED(args)) {
    Py_INCREF(self_obj);
    return self_obj;
}

static PyObject *uuid_deepcopy(PyObject *self_obj, PyObject *args) {
    PyObject *memo;

    if (!PyArg_ParseTuple(args, "O:__deepcopy__", &memo)) {
        return NULL;
    }

    Py_INCREF(self_obj);
    return self_obj;
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static Py_hash_t uuid_hash(PyObject *self_obj) {
    const UUIDObject *self = uuid_self_const(self_obj);
    Py_hash_t hash = (Py_hash_t)(self->hi ^ self->hi >> 32 ^ self->lo ^ self->lo >> 32);

    if (hash == -1) {
        hash = -2;
    }

    return hash;
}

static int uuid_compare(const UUIDObject *left, const UUIDObject *right) {
    if (left->hi != right->hi) {
        return left->hi < right->hi ? -1 : 1;
    }

    if (left->lo != right->lo) {
        return left->lo < right->lo ? -1 : 1;
    }

    return 0;
}

static PyObject *uuid_richcompare(PyObject *a, PyObject *b, const int op) {
    if (!PyObject_TypeCheck(a, &UUIDType) || !PyObject_TypeCheck(b, &UUIDType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    const UUIDObject *ua = (const UUIDObject *)a;
    const UUIDObject *ub = (const UUIDObject *)b;

    const int cmp = uuid_compare(ua, ub);

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
        break;
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyMethodDef uuid_methods[] = {
    {"__copy__", (PyCFunction)uuid_copy, METH_NOARGS, "Return self for copy.copy()."},
    {"__deepcopy__", (PyCFunction)uuid_deepcopy, METH_VARARGS, "Return self for copy.deepcopy()."},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef uuid_getset[] = {
    {"bytes", (getter)uuid_bytes, NULL, "UUID as 16 big-endian bytes.", NULL},
    {"bytes_le", (getter)uuid_bytes_le, NULL, "UUID as 16 little-endian bytes.", NULL},
    {"clock_seq", (getter)uuid_clock_seq, NULL, "Clock sequence.", NULL},
    {"fields", (getter)uuid_fields, NULL, "UUID fields tuple.", NULL},
    {"hex", (getter)uuid_hex, NULL, "Hexadecimal string.", NULL},
    {"int", (getter)uuid_int, NULL, "128-bit integer value.", NULL},
    {"time", (getter)uuid_timestamp, NULL, "UUID time value.", NULL},
    {"timestamp", (getter)uuid_timestamp, NULL, "Unix timestamp in milliseconds.", NULL},
    {"urn", (getter)uuid_urn, NULL, "UUID URN string.", NULL},
#define UUID_GETSET_ULONG_ENTRY(name, property_name, expr, doc)                                    \
    {property_name, ((getter)(name)), NULL, doc, NULL},
#define UUID_GETSET_ULL_ENTRY(name, property_name, expr, doc)                                      \
    {property_name, ((getter)(name)), NULL, doc, NULL},
    UUID_ULONG_GETTER_SPECS(UUID_GETSET_ULONG_ENTRY) UUID_ULL_GETTER_SPECS(UUID_GETSET_ULL_ENTRY)
#undef UUID_GETSET_ULONG_ENTRY
#undef UUID_GETSET_ULL_ENTRY
        {NULL, NULL, NULL, NULL, NULL},
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

static PyObject *py_uuid7(PyObject *Py_UNUSED(self),
                          PyObject *const *args,
                          const Py_ssize_t nargs,
                          PyObject *kwnames) {
    PyObject *timestamp_obj = Py_None;
    PyObject *nanos_obj = Py_None;
    PyObject *mode_obj = Py_None;
    const Py_ssize_t nkw = kwnames == NULL ? 0 : PyTuple_GET_SIZE(kwnames);
    UUID7Args parsed;
    uint64_t hi;
    uint64_t lo;
    int mode = MODE_FAST;

    if (nargs == 0 && nkw == 0) {
        return (PyObject *)uuid_new_with_generated_default();
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
    for (Py_ssize_t i = 0; i < nkw; ++i) {
        PyObject *key = PyTuple_GET_ITEM(kwnames, i);
        PyObject *value = args[nargs + i];

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

    if (timestamp_obj == Py_None && nanos_obj == Py_None) {
        if (build_uuid7_default(mode, &hi, &lo) != 0) {
            return NULL;
        }
    } else {
        if (ensure_seeded() != 0) {
            return NULL;
        }
        if (parse_args(timestamp_obj, nanos_obj, &parsed) != 0) {
            return NULL;
        }
        if (build_uuid7_with_parsed_args(&parsed, mode, &hi, &lo) != 0) {
            return NULL;
        }
    }

    return (PyObject *)uuid_new(hi, lo);
}

static PyObject *py_reseed_rng(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(args)) {
    random_reseed();
    last_timestamp_ms = 0;
    counter42 = 0;
    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    {"_uuid7",
     (PyCFunction)(void (*)(void))py_uuid7,
     METH_FASTCALL | METH_KEYWORDS,
     "Generate a fast UUIDv7 object."},
    {"_reseed_rng", py_reseed_rng, METH_NOARGS, "Reseed the internal RNG state."},
    {NULL, NULL, 0, NULL},
};

static PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_core",
    "Fast UUIDv7 generator.",
    -1,
    module_methods,
    .m_slots = NULL,
    .m_traverse = NULL,
    .m_clear = NULL,
    .m_free = NULL,
};

PyMODINIT_FUNC PyInit__core(void) {
    memset(&uuid_as_number, 0, sizeof(uuid_as_number));
    uuid_as_number.nb_int = (unaryfunc)uuid_nb_int;

    if (PyType_Ready(&UUIDType) < 0) {
        return NULL;
    }

    PyObject *module = PyModule_Create(&module_def);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&UUIDType);
    if (PyModule_AddObject(module, "_UUID", (PyObject *)&UUIDType) < 0) {
        Py_DECREF(&UUIDType);
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
