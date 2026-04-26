#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "hex/hex.h"
#include "platform.h"
#include "random.h"

#include <stdint.h>
#include <string.h>

#define PY_3_13 0x030D0000
#define PY_3_14 0x030E0000

#if PY_VERSION_HEX >= PY_3_14
    #define UUID_PyLong_FromU32(x) PyLong_FromUInt32((uint32_t)(x))
    #define UUID_PyLong_FromU64(x) PyLong_FromUInt64((uint64_t)(x))
#else
    #define UUID_PyLong_FromU32(x) PyLong_FromUnsignedLong((unsigned long)(x))
    #define UUID_PyLong_FromU64(x) PyLong_FromUnsignedLongLong((unsigned long long)(x))
#endif

#ifdef __GNUC__
    #define LIKELY(x) __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
#endif

typedef struct {
    PyObject_HEAD uint64_t hi;
    uint64_t lo;
} UUIDObject;

enum uuid7_mode : int {
    MODE_FAST = 0,
    MODE_SECURE = 1,
};

static PyTypeObject UUIDType;
static UUIDObject *uuid_cache = nullptr;
static PyObject *uuid_nb_int(PyObject *);
static PyNumberMethods uuid_as_number = {.nb_int = (unaryfunc)uuid_nb_int};

static uint64_t last_timestamp_ms = 0;
static uint64_t counter42 = 0;

constexpr unsigned int V7_TIMESTAMP_SHIFT = 16;
constexpr uint64_t V7_VERSION_BITS = 0x7000ULL;
constexpr uint64_t V7_VARIANT_BITS = 0x8000'0000'0000'0000ULL;
constexpr uint64_t V7_MAX_TIMESTAMP_MS = 0xFFFF'FFFF'FFFFULL;
constexpr uint64_t V7_MAX_TIMESTAMP_S = V7_MAX_TIMESTAMP_MS / 1000ULL;
constexpr uint64_t MAX_NANOS = 1'000'000'000ULL;
constexpr uint64_t V7_MAX_COUNTER = (1ULL << 42) - 1ULL;

static_assert(sizeof(uint64_t) == 8, "uint64_t must be 64-bit");
static_assert(sizeof(uint32_t) == 4, "uint32_t must be 32-bit");
static_assert(sizeof(uint16_t) == 2, "uint16_t must be 16-bit");

#define UUID_CONST(obj) ((const UUIDObject *)(obj))

#define UUID_INT32_GETTER(name, expr)                                                                                                      \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                                                                  \
        return UUID_PyLong_FromU32(UUID_CONST(self_obj)->expr);                                                                            \
    }

#define UUID_INT64_GETTER(name, expr)                                                                                                      \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                                                                  \
        return UUID_PyLong_FromU64(UUID_CONST(self_obj)->expr);                                                                            \
    }

static void reseed_generator_state(void) {
    random_reseed();
    last_timestamp_ms = 0;
    counter42 = 0;
}

static int parse_u64(PyObject *value, uint64_t *out, const char *name) {
    if (value == nullptr || value == Py_None) {
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
    bool has_timestamp;
    bool has_nanos;
} UUID7Args;

static int build_timestamp_ms(const uint64_t timestamp_s,
                              const bool has_timestamp,
                              const uint64_t nanos,
                              const bool has_nanos,
                              uint64_t *timestamp_ms) {
    if (!has_timestamp) {
        *timestamp_ms = now_ms();
        return 0;
    }

    if (timestamp_s > V7_MAX_TIMESTAMP_S) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    uint64_t ms = timestamp_s * 1000ULL;
    if (has_nanos) {
        ms += nanos / 1'000'000ULL;
    }

    if (ms > V7_MAX_TIMESTAMP_MS) {
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

    const int ts_state = parse_u64(timestamp_obj, &timestamp_s, "timestamp");
    if (ts_state < 0) {
        return -1;
    }
    parsed->has_timestamp = ts_state > 0;

    const int ns_state = parse_u64(nanos_obj, &parsed->nanos, "nanos");
    if (ns_state < 0) {
        return -1;
    }
    parsed->has_nanos = ns_state > 0;

    if (parsed->has_nanos && parsed->nanos >= MAX_NANOS) {
        PyErr_SetString(PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }

    return build_timestamp_ms(timestamp_s, parsed->has_timestamp, parsed->nanos, parsed->has_nanos, &parsed->timestamp_ms);
}

static int
advance_monotonic_impl(const uint64_t observed_ms, uint64_t *timestamp_ms, uint16_t *rand_a, uint64_t *tail62, const bool secure) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment = 0;
    uint32_t low32 = 0;

    if (secure) {
        if (random_next_low32_and_increment_secure(&low32, &increment) != 0) {
            return -1;
        }
    } else {
        random_next_low32_and_increment(&low32, &increment);
    }

    if (LIKELY(observed_ms > current_ms)) {
        current_ms = observed_ms;
        if (secure) {
            if (UNLIKELY(random_counter42_secure(&counter) != 0)) {
                return -1;
            }
        } else {
            counter = random_counter42();
        }
    } else {
        counter += increment;
        if (UNLIKELY(counter > V7_MAX_COUNTER)) {
            current_ms += 1U;
            if (secure) {
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

static void uuid_build_words(const uint64_t timestamp_ms, const uint16_t rand_a, const uint64_t tail62, uint64_t *hi, uint64_t *lo) {
    *hi = timestamp_ms << V7_TIMESTAMP_SHIFT | V7_VERSION_BITS | (uint64_t)rand_a;
    *lo = V7_VARIANT_BITS | tail62;
}

static int build_uuid7_default_impl(uint64_t *hi, uint64_t *lo, const bool secure) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (UNLIKELY(random_ensure_seeded() != 0)) {
        return -1;
    }

    if (UNLIKELY(advance_monotonic_impl(now_ms(), &timestamp_ms, &rand_a, &tail62, secure) != 0)) {
        return -1;
    }

    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int extract_random_bits_impl(const UUID7Args *args, uint16_t *rand_a, uint64_t *tail62, const bool secure) {
    if (args->has_timestamp && args->has_nanos) {
        *rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        if (secure) {
            return random_tail62_secure(tail62);
        }
        *tail62 = random_tail62();
        return 0;
    }

    if (args->has_timestamp || args->has_nanos) {
        if (secure) {
            return random_payload_secure(rand_a, tail62);
        }
        random_payload(rand_a, tail62);
        return 0;
    }

    return 1;
}

static int build_uuid7_with_args(const UUID7Args *args, uint64_t *hi, uint64_t *lo, const bool secure) {
    uint64_t tail62;
    uint16_t rand_a;

    const int state = extract_random_bits_impl(args, &rand_a, &tail62, secure);
    if (state < 0) {
        return -1;
    }

    if (state > 0) {
        uint64_t timestamp_ms = args->timestamp_ms;
        if (advance_monotonic_impl(timestamp_ms, &timestamp_ms, &rand_a, &tail62, secure) != 0) {
            return -1;
        }
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int
build_uuid7_parts_from_args(PyObject *timestamp_obj, PyObject *nanos_obj, const enum uuid7_mode mode, uint64_t *hi, uint64_t *lo) {
    UUID7Args parsed;

    if (random_ensure_seeded() != 0) {
        return -1;
    }

    if (parse_args(timestamp_obj, nanos_obj, &parsed) != 0) {
        return -1;
    }

    return build_uuid7_with_args(&parsed, hi, lo, mode == MODE_SECURE);
}

static int parse_mode(PyObject *value, enum uuid7_mode *mode) {
    if (value == nullptr || value == Py_None) {
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

static int parse_uuid_text(PyObject *value, uint64_t *hi, uint64_t *lo) {
    Py_ssize_t size = 0;

    if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "UUID() argument must be a str");
        return -1;
    }

    const char *text = PyUnicode_AsUTF8AndSize(value, &size);
    if (text == nullptr) {
        return -1;
    }

    if (parse_uuid_hex_str(text, (size_t)size, hi, lo) != 0) {
        PyErr_SetString(PyExc_ValueError, "badly formed hexadecimal UUID string");
        return -1;
    }

    return 0;
}

static int parse_uuid_bytes(PyObject *value, const bool little_endian, uint64_t *hi, uint64_t *lo) {
    char *buffer;
    Py_ssize_t length;
    unsigned char reordered[16];

    if (PyBytes_AsStringAndSize(value, &buffer, &length) != 0) {
        PyErr_SetString(PyExc_TypeError, "bytes must be a 16-char bytes object");
        return -1;
    }

    if (length != 16) {
        PyErr_SetString(PyExc_ValueError, "bytes is not a 16-char string");
        return -1;
    }

    const unsigned char *bytes = (const unsigned char *)buffer;
    if (little_endian) {
        uuid_to_bytes_le(bytes, reordered);
        bytes = reordered;
    }

    bytes_to_words(bytes, hi, lo);
    return 0;
}

static int parse_uuid_int(PyObject *value, uint64_t *hi, uint64_t *lo) {
    if (!PyLong_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "int must be a 128-bit integer");
        return -1;
    }

#if PY_VERSION_HEX >= PY_3_13
    unsigned char bytes[16];
    const Py_ssize_t nbytes = PyLong_AsNativeBytes(value, bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
    if (nbytes < 0) {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        }
        return -1;
    }
    if (nbytes > 16) {
        PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        return -1;
    }
    bytes_to_words(bytes, hi, lo);
    return 0;
#else
    PyObject *mask = PyLong_FromUnsignedLongLong(0xFFFF'FFFF'FFFF'FFFFULL);
    if (mask == nullptr) {
        return -1;
    }

    PyObject *part = PyNumber_And(value, mask);
    if (part == nullptr) {
        Py_DECREF(mask);
        return -1;
    }
    *lo = PyLong_AsUnsignedLongLong(part);
    Py_DECREF(part);
    if (PyErr_Occurred()) {
        Py_DECREF(mask);
        goto range_error;
    }

    PyObject *shift_by = PyLong_FromLong(64);
    if (shift_by == nullptr) {
        Py_DECREF(mask);
        return -1;
    }
    PyObject *shifted = PyNumber_Rshift(value, shift_by);
    if (shifted == nullptr) {
        Py_DECREF(mask);
        Py_DECREF(shift_by);
        return -1;
    }

    part = PyNumber_And(shifted, mask);
    Py_DECREF(mask);
    if (part == nullptr) {
        Py_DECREF(shifted);
        Py_DECREF(shift_by);
        return -1;
    }
    *hi = PyLong_AsUnsignedLongLong(part);
    Py_DECREF(part);
    if (PyErr_Occurred()) {
        Py_DECREF(shifted);
        Py_DECREF(shift_by);
        goto range_error;
    }

    PyObject *upper = PyNumber_Rshift(shifted, shift_by);
    Py_DECREF(shifted);
    Py_DECREF(shift_by);
    if (upper == nullptr) {
        return -1;
    }
    const int overflow = PyObject_IsTrue(upper);
    Py_DECREF(upper);
    if (overflow < 0) {
        return -1;
    }
    if (overflow) {
        goto range_error;
    }
    return 0;

range_error:
    PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
    return -1;
#endif
}

static int parse_uuid_fields(PyObject *value, uint64_t *hi, uint64_t *lo) {
    static constexpr uint64_t limits[6] = {
        0xFFFF'FFFFULL,
        0xFFFFULL,
        0xFFFFULL,
        0xFFULL,
        0xFFULL,
        0xFFFF'FFFF'FFFFULL,
    };
    uint64_t parts[6];

    PyObject *fast = PySequence_Fast(value, "fields must be a 6-tuple");
    if (fast == nullptr) {
        return -1;
    }

    if (PySequence_Fast_GET_SIZE(fast) != 6) {
        Py_DECREF(fast);
        PyErr_SetString(PyExc_ValueError, "fields is not a 6-tuple");
        return -1;
    }

    for (int i = 0; i < 6; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(fast, i);
        const unsigned long long temp = PyLong_AsUnsignedLongLong(item);

        if (PyErr_Occurred()) {
            Py_DECREF(fast);
            PyErr_SetString(PyExc_TypeError, "fields must contain only integers");
            return -1;
        }
        if (temp > limits[i]) {
            Py_DECREF(fast);
            PyErr_SetString(PyExc_ValueError, "field value out of range");
            return -1;
        }
        parts[i] = (uint64_t)temp;
    }

    Py_DECREF(fast);

    *hi = parts[0] << 32 | parts[1] << 16 | parts[2];
    *lo = parts[3] << 56 | parts[4] << 48 | parts[5];
    return 0;
}

static UUIDObject *uuid_new_uncached(const uint64_t hi, const uint64_t lo) {
    UUIDObject *obj = PyObject_New(UUIDObject, &UUIDType);

    if (obj == nullptr) {
        return nullptr;
    }

    obj->hi = hi;
    obj->lo = lo;
    return obj;
}

static UUIDObject *uuid_new(const uint64_t hi, const uint64_t lo) {
    if (LIKELY(uuid_cache != nullptr && Py_REFCNT(uuid_cache) == 1)) {
        Py_INCREF(uuid_cache);
        uuid_cache->hi = hi;
        uuid_cache->lo = lo;
        return uuid_cache;
    }

    UUIDObject *obj = PyObject_New(UUIDObject, &UUIDType);
    if (UNLIKELY(obj == nullptr)) {
        return nullptr;
    }

    obj->hi = hi;
    obj->lo = lo;

    if (uuid_cache == nullptr) {
        uuid_cache = obj;
        Py_INCREF(uuid_cache);
    }

    return obj;
}

static PyObject *uuid_type_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"hex", "bytes", "bytes_le", "fields", "int", nullptr};
    PyObject *hex = Py_None;
    PyObject *bytes = Py_None;
    PyObject *bytes_le = Py_None;
    PyObject *fields = Py_None;
    PyObject *int_value = Py_None;
    uint64_t hi = 0;
    uint64_t lo = 0;

    if (type != &UUIDType) {
        return type->tp_alloc(type, 0);
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O$OOOO:UUID", kwlist, &hex, &bytes, &bytes_le, &fields, &int_value)) {
        return nullptr;
    }

    const int provided = (hex != Py_None) + (bytes != Py_None) + (bytes_le != Py_None) + (fields != Py_None) + (int_value != Py_None);

    if (provided != 1) {
        PyErr_SetString(PyExc_TypeError, "one of the hex, bytes, bytes_le, fields, or int arguments must be given");
        return nullptr;
    }

    if (PyObject_TypeCheck(hex, &UUIDType)) {
        Py_INCREF(hex);
        return hex;
    }

    if (hex != Py_None) {
        if (parse_uuid_text(hex, &hi, &lo) != 0) {
            return nullptr;
        }
    } else if (bytes != Py_None) {
        if (parse_uuid_bytes(bytes, false, &hi, &lo) != 0) {
            return nullptr;
        }
    } else if (bytes_le != Py_None) {
        if (parse_uuid_bytes(bytes_le, true, &hi, &lo) != 0) {
            return nullptr;
        }
    } else if (fields != Py_None) {
        if (parse_uuid_fields(fields, &hi, &lo) != 0) {
            return nullptr;
        }
    } else if (parse_uuid_int(int_value, &hi, &lo) != 0) {
        return nullptr;
    }

    return (PyObject *)uuid_new(hi, lo);
}

static PyObject *__str__(PyObject *self_obj) {
    PyObject *str = PyUnicode_New(36, 127);
    if (str == nullptr) {
        return nullptr;
    }
    const UUIDObject *self = UUID_CONST(self_obj);
    fmt_dashed(self->hi, self->lo, (char *)PyUnicode_1BYTE_DATA(str));
    return str;
}

static PyObject *__repr__(PyObject *self_obj) {
    PyObject *str = PyUnicode_New(44, 127);
    if (str == nullptr) {
        return nullptr;
    }
    char *out = (char *)PyUnicode_1BYTE_DATA(str);
    const UUIDObject *self = UUID_CONST(self_obj);
    memcpy(out, "UUID('", 6);
    fmt_dashed(self->hi, self->lo, out + 6);
    memcpy(out + 42, "')", 2);
    return str;
}

static PyObject *uuid_hex(PyObject *self_obj, void *Py_UNUSED(closure)) {
    PyObject *str = PyUnicode_New(32, 127);
    if (str == nullptr) {
        return nullptr;
    }
    const UUIDObject *self = UUID_CONST(self_obj);
    fmt_hex32(self->hi, self->lo, (char *)PyUnicode_1BYTE_DATA(str));
    return str;
}

static PyObject *uuid_bytes(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    const UUIDObject *self = UUID_CONST(self_obj);

    uuid_to_bytes(self->hi, self->lo, bytes);
    return PyBytes_FromStringAndSize((const char *)bytes, 16);
}

static PyObject *uuid_bytes_le(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    unsigned char reordered[16];
    const UUIDObject *self = UUID_CONST(self_obj);

    uuid_to_bytes(self->hi, self->lo, bytes);
    uuid_to_bytes_le(bytes, reordered);
    return PyBytes_FromStringAndSize((const char *)reordered, 16);
}

UUID_INT64_GETTER(uuid_timestamp, hi >> V7_TIMESTAMP_SHIFT)

static PyObject *uuid_int_from_parts(const uint64_t hi, const uint64_t lo) {
#if PY_VERSION_HEX >= PY_3_13
    unsigned char bytes[16];
    uuid_to_bytes(hi, lo, bytes);
    return PyLong_FromUnsignedNativeBytes(bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
#else
    PyObject *high = PyLong_FromUnsignedLongLong(hi);
    if (high == nullptr) {
        return nullptr;
    }

    PyObject *bits = PyLong_FromLong(64);
    if (bits == nullptr) {
        Py_DECREF(high);
        return nullptr;
    }

    PyObject *shifted = PyNumber_Lshift(high, bits);
    Py_DECREF(high);
    Py_DECREF(bits);
    if (shifted == nullptr) {
        return nullptr;
    }

    PyObject *low = PyLong_FromUnsignedLongLong(lo);
    if (low == nullptr) {
        Py_DECREF(shifted);
        return nullptr;
    }

    PyObject *result = PyNumber_Or(shifted, low);
    Py_DECREF(shifted);
    Py_DECREF(low);
    return result;
#endif
}

static PyObject *uuid_int(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);
    return uuid_int_from_parts(self->hi, self->lo);
}

static PyObject *uuid_nb_int(PyObject *self_obj) {
    return uuid_int(self_obj, nullptr);
}

UUID_INT32_GETTER(uuid_time_low, hi >> 32)
UUID_INT32_GETTER(uuid_time_mid, hi >> 16 & 0xFFFFULL)
UUID_INT32_GETTER(uuid_time_hi_version, hi & 0xFFFFULL)
UUID_INT32_GETTER(uuid_clock_seq_hi_variant, lo >> 56)
UUID_INT32_GETTER(uuid_clock_seq_low, lo >> 48 & 0xFFULL)

static PyObject *uuid_clock_seq(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);
    const uint32_t high = (uint32_t)(self->lo >> 56 & 0x3FULL);
    const uint32_t low = (uint32_t)(self->lo >> 48 & 0xFFULL);
    return UUID_PyLong_FromU32(high << 8 | low);
}

UUID_INT64_GETTER(uuid_node, lo & 0xFFFF'FFFF'FFFFULL)

static PyObject *uuid_fields(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);
    return Py_BuildValue("(kkkkkK)",
                         (unsigned long)(self->hi >> 32),
                         (unsigned long)(self->hi >> 16 & 0xFFFFULL),
                         (unsigned long)(self->hi & 0xFFFFULL),
                         (unsigned long)(self->lo >> 56),
                         (unsigned long)(self->lo >> 48 & 0xFFULL),
                         (unsigned long long)(self->lo & 0xFFFF'FFFF'FFFFULL));
}

static PyObject *uuid_urn(PyObject *self_obj, void *Py_UNUSED(closure)) {
    PyObject *str = PyUnicode_New(45, 127);
    if (str == nullptr) {
        return nullptr;
    }
    char *out = (char *)PyUnicode_1BYTE_DATA(str);
    const UUIDObject *self = UUID_CONST(self_obj);
    memcpy(out, "urn:uuid:", 9);
    fmt_dashed(self->hi, self->lo, out + 9);
    return str;
}

static PyObject *__copy__(PyObject *self_obj, PyObject *Py_UNUSED(ignored)) {
    Py_INCREF(self_obj);
    return self_obj;
}

static Py_hash_t __hash__(PyObject *self_obj) {
    const UUIDObject *self = UUID_CONST(self_obj);
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

static PyObject *richcompare(PyObject *a, PyObject *b, const int op) {
    if (!PyObject_TypeCheck(a, &UUIDType) || !PyObject_TypeCheck(b, &UUIDType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    const int cmp = uuid_compare((const UUIDObject *)a, (const UUIDObject *)b);
    Py_RETURN_RICHCOMPARE(cmp, 0, op);
}

static PyMethodDef uuid_methods[] = {
    {"__copy__", (PyCFunction)__copy__, METH_NOARGS, "Return self for copy.copy()."},
    {"__deepcopy__", (PyCFunction)__copy__, METH_O, "Return self for copy.deepcopy()."},
    {nullptr, nullptr, 0, nullptr},
};

static PyGetSetDef uuid_getset[] = {
    {"bytes", (getter)uuid_bytes, nullptr, "UUID as 16 big-endian bytes.", nullptr},
    {"bytes_le", (getter)uuid_bytes_le, nullptr, "UUID as 16 little-endian bytes.", nullptr},
    {"clock_seq", (getter)uuid_clock_seq, nullptr, "Clock sequence.", nullptr},
    {"clock_seq_hi_variant", (getter)uuid_clock_seq_hi_variant, nullptr, "Clock sequence high byte with variant.", nullptr},
    {"clock_seq_low", (getter)uuid_clock_seq_low, nullptr, "Clock sequence low byte.", nullptr},
    {"fields", (getter)uuid_fields, nullptr, "UUID fields tuple.", nullptr},
    {"hex", (getter)uuid_hex, nullptr, "Hexadecimal string.", nullptr},
    {"int", (getter)uuid_int, nullptr, "128-bit integer value.", nullptr},
    {"node", (getter)uuid_node, nullptr, "Node value.", nullptr},
    {"time", (getter)uuid_timestamp, nullptr, "UUID time value.", nullptr},
    {"time_hi_version", (getter)uuid_time_hi_version, nullptr, "Time high field with version bits.", nullptr},
    {"time_low", (getter)uuid_time_low, nullptr, "Time low field.", nullptr},
    {"time_mid", (getter)uuid_time_mid, nullptr, "Time middle field.", nullptr},
    {"timestamp", (getter)uuid_timestamp, nullptr, "Unix timestamp in milliseconds.", nullptr},
    {"urn", (getter)uuid_urn, nullptr, "UUID URN string.", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

static PyTypeObject UUIDType = {
    PyVarObject_HEAD_INIT(nullptr, 0).tp_name = "c_uuid_v7.UUID",
    .tp_basicsize = sizeof(UUIDObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = uuid_type_new,
    .tp_repr = (reprfunc)__repr__,
    .tp_str = (reprfunc)__str__,
    .tp_hash = (hashfunc)__hash__,
    .tp_richcompare = richcompare,
    .tp_methods = uuid_methods,
    .tp_getset = uuid_getset,
    .tp_as_number = &uuid_as_number,
};

static PyObject *py_uuid7(PyObject *Py_UNUSED(self), PyObject *const *args, const Py_ssize_t nargs, PyObject *kwnames) {
    PyObject *timestamp_obj = Py_None;
    PyObject *nanos_obj = Py_None;
    PyObject *mode_obj = Py_None;
    const Py_ssize_t nkw = kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames);
    uint64_t hi = 0;
    uint64_t lo = 0;
    enum uuid7_mode mode = MODE_FAST;

    if (LIKELY(nargs == 0 && nkw == 0)) {
        if (UNLIKELY(build_uuid7_default_impl(&hi, &lo, false) != 0)) {
            return nullptr;
        }
        return (PyObject *)uuid_new(hi, lo);
    }

    if (nargs > 3) {
        PyErr_SetString(PyExc_TypeError, "uuid7() takes at most 3 positional arguments");
        return nullptr;
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
            return nullptr;
        }
    }

    if (parse_mode(mode_obj, &mode) != 0) {
        return nullptr;
    }

    if (mode == MODE_SECURE && timestamp_obj == Py_None && nanos_obj == Py_None) {
        if (build_uuid7_default_impl(&hi, &lo, true) != 0) {
            return nullptr;
        }
    } else if (build_uuid7_parts_from_args(timestamp_obj, nanos_obj, mode, &hi, &lo) != 0) {
        return nullptr;
    }

    return (PyObject *)uuid_new(hi, lo);
}

static PyObject *py_reseed_rng(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(args)) {
    reseed_generator_state();
    Py_RETURN_NONE;
}

static int add_module(PyObject *module, const char *name, const uint64_t hi, const uint64_t lo) {
    UUIDObject *value = uuid_new_uncached(hi, lo);
    if (value == nullptr) {
        return -1;
    }
#if PY_VERSION_HEX >= PY_3_13
    return PyModule_Add(module, name, (PyObject *)value);
#else
    if (PyModule_AddObject(module, name, (PyObject *)value) < 0) {
        Py_DECREF(value);
        return -1;
    }
    return 0;
#endif
}

static PyMethodDef module_methods[] = {
    {"_uuid7", (PyCFunction)(void (*)(void))py_uuid7, METH_FASTCALL | METH_KEYWORDS, "Generate a UUIDv7."},
    {"_reseed_rng", py_reseed_rng, METH_NOARGS, "Reseed the random number generator."},
    {nullptr, nullptr, 0, nullptr},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_core",
    "Fast UUIDv7 generator.",
    -1,
    module_methods,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

PyMODINIT_FUNC PyInit__core(void) {
    if (PyType_Ready(&UUIDType) < 0) {
        return nullptr;
    }

    PyObject *module = PyModule_Create(&module_def);
    if (module == nullptr) {
        return nullptr;
    }

#if PY_VERSION_HEX >= PY_3_13
    if (PyModule_Add(module, "_UUID", Py_NewRef(&UUIDType)) < 0) {
        goto error;
    }
#else
    Py_INCREF(&UUIDType);
    if (PyModule_AddObject(module, "_UUID", (PyObject *)&UUIDType) < 0) {
        Py_DECREF(&UUIDType);
        goto error;
    }
#endif

    if (add_module(module, "_NIL", 0, 0) < 0) {
        goto error;
    }

    if (add_module(module, "_MAX", UINT64_MAX, UINT64_MAX) < 0) {
        goto error;
    }

    return module;

error:
    Py_DECREF(module);
    return nullptr;
}
