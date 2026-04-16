#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "helpers/hexpairs.h"
#include "helpers/hexparse.h"
#include "helpers/words.h"
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

#define V7_TIMESTAMP_SHIFT 16
#define V7_VERSION_BITS 0x7000ULL
#define V7_VARIANT_BITS 0x8000000000000000ULL
#define V7_MAX_TIMESTAMP_MS 0xFFFFFFFFFFFFULL
#define V7_MAX_TIMESTAMP_S (V7_MAX_TIMESTAMP_MS / 1000ULL)
#define MAX_NANOS 1000000000ULL
#define V7_MAX_COUNTER ((1ULL << 42) - 1ULL)
#define MODE_FAST 0
#define MODE_SECURE 1

#define UUID_CONST(obj) ((const UUIDObject *)(obj))

#define UUID_INT32_GETTER(name, expr)                                                                                                      \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                                                                  \
        return PyLong_FromUnsignedLong((unsigned long)(UUID_CONST(self_obj)->expr));                                                       \
    }

#define UUID_INT64_GETTER(name, expr)                                                                                                      \
    static PyObject *name(PyObject *self_obj, void *Py_UNUSED(closure)) {                                                                  \
        return PyLong_FromUnsignedLongLong((unsigned long long)(UUID_CONST(self_obj)->expr));                                              \
    }

static void reseed_generator_state(void) {
    random_reseed();
    last_timestamp_ms = 0;
    counter42 = 0;
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

static int extract_random_bits(const UUID7Args *args, uint16_t *rand_a, uint64_t *tail62);

static int validate_nanos(const uint64_t nanos) {
    if (nanos >= MAX_NANOS) {
        PyErr_SetString(PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }
    return 0;
}

static int
build_timestamp_ms(const uint64_t timestamp_s, const int has_timestamp, const uint64_t nanos, const int has_nanos, uint64_t *timestamp_ms) {
    uint64_t ms = 0;

    if (!has_timestamp) {
        *timestamp_ms = now_ms();
        return 0;
    }

    if (timestamp_s > V7_MAX_TIMESTAMP_S) {
        PyErr_SetString(PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    ms = timestamp_s * 1000ULL;
    if (has_nanos) {
        ms += nanos / 1000000ULL;
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

    parsed->has_timestamp = parse_u64(timestamp_obj, &timestamp_s, "timestamp");
    if (parsed->has_timestamp < 0) {
        return -1;
    }

    parsed->has_nanos = parse_u64(nanos_obj, &parsed->nanos, "nanos");
    if (parsed->has_nanos < 0) {
        return -1;
    }

    if (parsed->has_nanos && validate_nanos(parsed->nanos) != 0) {
        return -1;
    }

    return build_timestamp_ms(timestamp_s, parsed->has_timestamp, parsed->nanos, parsed->has_nanos, &parsed->timestamp_ms);
}

static void advance_monotonic(const uint64_t observed_ms, uint64_t *timestamp_ms, uint16_t *rand_a, uint64_t *tail62) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment;
    uint32_t low32;

    random_next_low32_and_increment(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = random_counter42();
    } else {
        counter += increment;
        if (counter > V7_MAX_COUNTER) {
            current_ms += 1U;
            counter = random_counter42();
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    *timestamp_ms = current_ms;
    random_split_counter42(counter, low32, rand_a, tail62);
}

static int advance_monotonic_secure(const uint64_t observed_ms, uint64_t *timestamp_ms, uint16_t *rand_a, uint64_t *tail62) {
    uint64_t counter = counter42;
    uint64_t current_ms = last_timestamp_ms;
    uint64_t increment;
    uint32_t low32;

    if (random_next_low32_and_increment_secure(&low32, &increment) != 0) {
        return -1;
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (random_counter42_secure(&counter) != 0) {
            return -1;
        }
    } else {
        counter += increment;
        if (counter > V7_MAX_COUNTER) {
            current_ms += 1U;
            if (random_counter42_secure(&counter) != 0) {
                return -1;
            }
        }
    }

    last_timestamp_ms = current_ms;
    counter42 = counter;
    *timestamp_ms = current_ms;
    random_split_counter42(counter, low32, rand_a, tail62);
    return 0;
}

static void uuid_to_bytes(const uint64_t hi, const uint64_t lo, unsigned char bytes[16]) {
    for (int i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)(hi >> (56 - i * 8));
        bytes[i + 8] = (unsigned char)(lo >> (56 - i * 8));
    }
}

static void uuid_build_words(const uint64_t timestamp_ms, const uint16_t rand_a, const uint64_t tail62, uint64_t *hi, uint64_t *lo) {
    *hi = timestamp_ms << V7_TIMESTAMP_SHIFT | V7_VERSION_BITS | (uint64_t)rand_a;
    *lo = V7_VARIANT_BITS | tail62;
}

static int build_uuid7_default(uint64_t *hi, uint64_t *lo) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (random_ensure_seeded() != 0) {
        return -1;
    }

    advance_monotonic(now_ms(), &timestamp_ms, &rand_a, &tail62);
    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int build_uuid7_default_secure(uint64_t *hi, uint64_t *lo) {
    uint64_t timestamp_ms;
    uint64_t tail62;
    uint16_t rand_a;

    if (random_ensure_seeded() != 0) {
        return -1;
    }

    if (advance_monotonic_secure(now_ms(), &timestamp_ms, &rand_a, &tail62) != 0) {
        return -1;
    }
    uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int build_uuid7_fast(const UUID7Args *args, uint64_t *hi, uint64_t *lo) {
    uint64_t tail62;
    uint16_t rand_a;

    if (random_ensure_seeded() != 0) {
        return -1;
    }

    const int state = extract_random_bits(args, &rand_a, &tail62);

    if (state > 0) {
        uint64_t timestamp_ms = args->timestamp_ms;

        advance_monotonic(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int extract_random_bits(const UUID7Args *args, uint16_t *rand_a, uint64_t *tail62) {
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

static int extract_uuid7_random_bits_secure(const UUID7Args *args, uint16_t *rand_a, uint64_t *tail62) {
    if (args->has_timestamp && args->has_nanos) {
        *rand_a = (uint16_t)(args->nanos & 0x0FFFU);
        return random_tail62_secure(tail62);
    }

    if (args->has_timestamp || args->has_nanos) {
        return random_payload_secure(rand_a, tail62);
    }

    return 1;
}

static int build_uuid7_secure(const UUID7Args *args, uint64_t *hi, uint64_t *lo) {
    uint64_t tail62;
    uint16_t rand_a;

    const int state = extract_uuid7_random_bits_secure(args, &rand_a, &tail62);

    if (state < 0) {
        return -1;
    }
    if (state > 0) {
        uint64_t timestamp_ms = args->timestamp_ms;

        if (advance_monotonic_secure(timestamp_ms, &timestamp_ms, &rand_a, &tail62) != 0) {
            return -1;
        }
        uuid_build_words(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuid_build_words(args->timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

static int build_uuid7_parts_from_args(PyObject *timestamp_obj, PyObject *nanos_obj, const int mode, uint64_t *hi, uint64_t *lo) {
    UUID7Args parsed;

    if (random_ensure_seeded() != 0) {
        return -1;
    }

    if (parse_args(timestamp_obj, nanos_obj, &parsed) != 0) {
        return -1;
    }

    if (mode == MODE_SECURE) {
        return build_uuid7_secure(&parsed, hi, lo);
    }

    return build_uuid7_fast(&parsed, hi, lo);
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

static int parse_uuid_text(PyObject *value, uint64_t *hi, uint64_t *lo) {
    Py_ssize_t size = 0;

    if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "UUID() argument must be a str");
        return -1;
    }

    const char *text = PyUnicode_AsUTF8AndSize(value, &size);
    if (text == NULL) {
        return -1;
    }

    if (parse_uuid_hex(text, (size_t)size, hi, lo) != 0) {
        PyErr_SetString(PyExc_ValueError, "badly formed hexadecimal UUID string");
        return -1;
    }

    return 0;
}

static int parse_uuid_bytes(PyObject *value, const int little_endian, uint64_t *hi, uint64_t *lo) {
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
        reordered[0] = bytes[3];
        reordered[1] = bytes[2];
        reordered[2] = bytes[1];
        reordered[3] = bytes[0];
        reordered[4] = bytes[5];
        reordered[5] = bytes[4];
        reordered[6] = bytes[7];
        reordered[7] = bytes[6];
        memcpy(reordered + 8, bytes + 8, 8);
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

#if PY_VERSION_HEX >= 0x030D0000
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
#else
    PyObject *zero = NULL;
    PyObject *shifted = NULL;
    PyObject *mask = NULL;
    PyObject *part = NULL;
    PyObject *shift_by = NULL;
    int is_negative;

    zero = PyLong_FromLong(0);
    if (zero == NULL) {
        return -1;
    }

    is_negative = PyObject_RichCompareBool(value, zero, Py_LT);
    Py_DECREF(zero);
    if (is_negative < 0) {
        return -1;
    }
    if (is_negative) {
        PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        return -1;
    }

    mask = PyLong_FromUnsignedLongLong(0xFFFFFFFFFFFFFFFFULL);
    if (mask == NULL) {
        return -1;
    }

    part = PyNumber_And(value, mask);
    if (part == NULL) {
        Py_DECREF(mask);
        return -1;
    }
    *lo = PyLong_AsUnsignedLongLong(part);
    Py_DECREF(part);
    if (PyErr_Occurred()) {
        Py_DECREF(mask);
        PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        return -1;
    }

    shift_by = PyLong_FromLong(64);
    if (shift_by == NULL) {
        Py_DECREF(mask);
        return -1;
    }

    shifted = PyNumber_Rshift(value, shift_by);
    Py_DECREF(shift_by);
    if (shifted == NULL) {
        Py_DECREF(mask);
        return -1;
    }

    part = PyNumber_And(shifted, mask);
    Py_DECREF(mask);
    Py_DECREF(shifted);
    if (part == NULL) {
        return -1;
    }
    *hi = PyLong_AsUnsignedLongLong(part);
    Py_DECREF(part);
    if (PyErr_Occurred()) {
        PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        return -1;
    }

    shift_by = PyLong_FromLong(128);
    if (shift_by == NULL) {
        return -1;
    }

    shifted = PyNumber_Rshift(value, shift_by);
    Py_DECREF(shift_by);
    if (shifted == NULL) {
        return -1;
    }
    const int overflow = PyObject_IsTrue(shifted);
    Py_DECREF(shifted);
    if (overflow < 0) {
        return -1;
    }
    if (overflow) {
        PyErr_SetString(PyExc_ValueError, "int is out of range (need a 128-bit value)");
        return -1;
    }

    return 0;
#endif

#if PY_VERSION_HEX >= 0x030D0000
    bytes_to_words(bytes, hi, lo);
#endif

    return 0;
}

static int parse_uuid_fields(PyObject *value, uint64_t *hi, uint64_t *lo) {
    PyObject *fast = NULL;
    uint64_t parts[6];
    static const uint64_t limits[] = {
        0xFFFFFFFFULL,
        0xFFFFULL,
        0xFFFFULL,
        0xFFULL,
        0xFFULL,
        0xFFFFFFFFFFFFULL,
    };

    fast = PySequence_Fast(value, "fields must be a 6-tuple");
    if (fast == NULL) {
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

static UUIDObject *uuid_new_permanent(const uint64_t hi, const uint64_t lo) {
    UUIDObject *obj = PyObject_New(UUIDObject, &UUIDType);

    if (obj == NULL) {
        return NULL;
    }

    obj->hi = hi;
    obj->lo = lo;
    return obj;
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

static PyObject *uuid_type_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"hex", "bytes", "bytes_le", "fields", "int", NULL};
    PyObject *hex = Py_None;
    PyObject *bytes = Py_None;
    PyObject *bytes_le = Py_None;
    PyObject *fields = Py_None;
    PyObject *int_value = Py_None;
    uint64_t hi;
    uint64_t lo;

    if (type != &UUIDType) {
        return type->tp_alloc(type, 0);
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOOOO:UUID", kwlist, &hex, &bytes, &bytes_le, &fields, &int_value)) {
        return NULL;
    }

    const int provided = (hex != Py_None) + (bytes != Py_None) + (bytes_le != Py_None) + (fields != Py_None) + (int_value != Py_None);

    if (provided != 1) {
        PyErr_SetString(PyExc_TypeError, "one of the hex, bytes, bytes_le, fields, or int arguments must be given");
        return NULL;
    }

    if (PyObject_TypeCheck(hex, &UUIDType)) {
        Py_INCREF(hex);
        return hex;
    }

    if (hex != Py_None) {
        if (parse_uuid_text(hex, &hi, &lo) != 0) {
            return NULL;
        }
    } else if (bytes != Py_None) {
        if (parse_uuid_bytes(bytes, 0, &hi, &lo) != 0) {
            return NULL;
        }
    } else if (bytes_le != Py_None) {
        if (parse_uuid_bytes(bytes_le, 1, &hi, &lo) != 0) {
            return NULL;
        }
    } else if (fields != Py_None) {
        if (parse_uuid_fields(fields, &hi, &lo) != 0) {
            return NULL;
        }
    } else if (parse_uuid_int(int_value, &hi, &lo) != 0) {
        return NULL;
    }

    return (PyObject *)uuid_new(hi, lo);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_str(PyObject *self_obj) {
    char out[36];
    const UUIDObject *self = UUID_CONST(self_obj);
    int j = 0;

    for (int shift = 56; shift >= 0; shift -= 8) {
        if (j == 8 || j == 13) {
            out[j++] = '-';
        }
        hex_pair(out + j, (unsigned char)(self->hi >> shift));
        j += 2;
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        if (j == 18 || j == 23) {
            out[j++] = '-';
        }
        hex_pair(out + j, (unsigned char)(self->lo >> shift));
        j += 2;
    }

    return PyUnicode_FromStringAndSize(out, 36);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_repr(PyObject *self_obj) {
    PyObject *text = uuid_str(self_obj);

    if (text == NULL) {
        return NULL;
    }

    PyObject *result = PyUnicode_FromFormat("UUID('%U')", text);
    Py_DECREF(text);
    return result;
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_hex(PyObject *self_obj, void *Py_UNUSED(closure)) {
    char out[32];
    const UUIDObject *self = UUID_CONST(self_obj);
    int j = 0;

    for (int shift = 56; shift >= 0; shift -= 8) {
        hex_pair(out + j, (unsigned char)(self->hi >> shift));
        j += 2;
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        hex_pair(out + j, (unsigned char)(self->lo >> shift));
        j += 2;
    }

    return PyUnicode_FromStringAndSize(out, 32);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_bytes(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    const UUIDObject *self = UUID_CONST(self_obj);

    uuid_to_bytes(self->hi, self->lo, bytes);
    // ReSharper disable once CppRedundantCastExpression
    return PyBytes_FromStringAndSize((const char *)bytes, 16);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_bytes_le(PyObject *self_obj, void *Py_UNUSED(closure)) {
    unsigned char bytes[16];
    unsigned char reordered[16];
    const UUIDObject *self = UUID_CONST(self_obj);

    uuid_to_bytes(self->hi, self->lo, bytes);
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

UUID_INT64_GETTER(uuid_timestamp, hi >> V7_TIMESTAMP_SHIFT)

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_int(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);

#if PY_VERSION_HEX >= 0x030D0000
    unsigned char bytes[16];

    uuid_to_bytes(self->hi, self->lo, bytes);

    return PyLong_FromUnsignedNativeBytes(bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
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

UUID_INT32_GETTER(uuid_time_low, hi >> 32)
UUID_INT32_GETTER(uuid_time_mid, hi >> 16 & 0xFFFFULL)
UUID_INT32_GETTER(uuid_time_hi_version, hi & 0xFFFFULL)
UUID_INT32_GETTER(uuid_clock_seq_hi_variant, lo >> 56)
UUID_INT32_GETTER(uuid_clock_seq_low, lo >> 48 & 0xFFULL)

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_clock_seq(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);
    const unsigned long high = (unsigned long)(self->lo >> 56 & 0x3FULL);
    const unsigned long low = (unsigned long)(self->lo >> 48 & 0xFFULL);
    return PyLong_FromUnsignedLong(high << 8 | low);
}

UUID_INT64_GETTER(uuid_node, lo & 0xFFFFFFFFFFFFULL)

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static PyObject *uuid_fields(PyObject *self_obj, void *Py_UNUSED(closure)) {
    const UUIDObject *self = UUID_CONST(self_obj);
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
    PyObject *text = uuid_str(self_obj);
    if (text == NULL) {
        return NULL;
    }

    PyObject *result = PyUnicode_FromFormat("urn:uuid:%U", text);
    Py_DECREF(text);
    return result;
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

static PyObject *uuid_richcompare(PyObject *a, PyObject *b, const int op) {
    if (!PyObject_TypeCheck(a, &UUIDType) || !PyObject_TypeCheck(b, &UUIDType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    const UUIDObject *ua = (const UUIDObject *)a;
    const UUIDObject *ub = (const UUIDObject *)b;

    const int cmp = uuid_compare(ua, ub);

    if (op == Py_LT) {
        return PyBool_FromLong(cmp < 0);
    }
    if (op == Py_LE) {
        return PyBool_FromLong(cmp <= 0);
    }
    if (op == Py_EQ) {
        return PyBool_FromLong(cmp == 0);
    }
    if (op == Py_NE) {
        return PyBool_FromLong(cmp != 0);
    }
    if (op == Py_GT) {
        return PyBool_FromLong(cmp > 0);
    }
    if (op == Py_GE) {
        return PyBool_FromLong(cmp >= 0);
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
    {"clock_seq_hi_variant", (getter)uuid_clock_seq_hi_variant, NULL, "Clock sequence high byte with variant.", NULL},
    {"clock_seq_low", (getter)uuid_clock_seq_low, NULL, "Clock sequence low byte.", NULL},
    {"fields", (getter)uuid_fields, NULL, "UUID fields tuple.", NULL},
    {"hex", (getter)uuid_hex, NULL, "Hexadecimal string.", NULL},
    {"int", (getter)uuid_int, NULL, "128-bit integer value.", NULL},
    {"node", (getter)uuid_node, NULL, "Node value.", NULL},
    {"time", (getter)uuid_timestamp, NULL, "UUID time value.", NULL},
    {"time_hi_version", (getter)uuid_time_hi_version, NULL, "Time high field with version bits.", NULL},
    {"time_low", (getter)uuid_time_low, NULL, "Time low field.", NULL},
    {"time_mid", (getter)uuid_time_mid, NULL, "Time middle field.", NULL},
    {"timestamp", (getter)uuid_timestamp, NULL, "Unix timestamp in milliseconds.", NULL},
    {"urn", (getter)uuid_urn, NULL, "UUID URN string.", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyTypeObject UUIDType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "c_uuid_v7.UUID",
    .tp_basicsize = sizeof(UUIDObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = uuid_type_new,
    .tp_repr = (reprfunc)uuid_repr,
    .tp_str = (reprfunc)uuid_str,
    .tp_hash = (hashfunc)uuid_hash,
    .tp_richcompare = uuid_richcompare,
    .tp_methods = uuid_methods,
    .tp_getset = uuid_getset,
    .tp_as_number = &uuid_as_number,
};

static PyObject *py_uuid7(PyObject *Py_UNUSED(self), PyObject *const *args, const Py_ssize_t nargs, PyObject *kwnames) {
    PyObject *timestamp_obj = Py_None;
    PyObject *nanos_obj = Py_None;
    PyObject *mode_obj = Py_None;
    const Py_ssize_t nkw = kwnames == NULL ? 0 : PyTuple_GET_SIZE(kwnames);
    uint64_t hi;
    uint64_t lo;
    int mode = MODE_FAST;

    if (nargs == 0 && nkw == 0) {
        if (build_uuid7_default(&hi, &lo) != 0) {
            return NULL;
        }
        return (PyObject *)uuid_new(hi, lo);
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

    if (mode == MODE_SECURE && timestamp_obj == Py_None && nanos_obj == Py_None) {
        if (build_uuid7_default_secure(&hi, &lo) != 0) {
            return NULL;
        }
    } else if (build_uuid7_parts_from_args(timestamp_obj, nanos_obj, mode, &hi, &lo) != 0) {
        return NULL;
    }

    return (PyObject *)uuid_new(hi, lo);
}

static PyObject *py_reseed_rng(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(args)) {
    reseed_generator_state();
    Py_RETURN_NONE;
}

static int add_module_uuid(PyObject *module, const char *name, const uint64_t hi, const uint64_t lo) {
    UUIDObject *value = uuid_new_permanent(hi, lo);

    if (value == NULL) {
        return -1;
    }
    if (PyModule_AddObject(module, name, (PyObject *)value) < 0) {
        Py_DECREF(value);
        return -1;
    }

    return 0;
}

static PyMethodDef module_methods[] = {
    {"_uuid7", (PyCFunction)(void (*)(void))py_uuid7, METH_FASTCALL | METH_KEYWORDS, "Generate a fast UUIDv7 object."},
    {"_reseed_rng", py_reseed_rng, METH_NOARGS, "Reseed the internal RNG state."},
    {NULL, NULL, 0, NULL},
};

static PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_core",
    "Fast UUIDv7 generator.",
    -1,
    module_methods,
    NULL,
    NULL,
    NULL,
    NULL,
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

    if (add_module_uuid(module, "_NIL", 0, 0) < 0) {
        Py_DECREF(module);
        return NULL;
    }

    if (add_module_uuid(module, "_MAX", UINT64_MAX, UINT64_MAX) < 0) {
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
