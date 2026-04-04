#include <hpy.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t hi;
    uint64_t lo;
} UUIDObject;

HPyType_HELPERS(UUIDObject)

enum {
    UUID_MODE_FAST = 0,
    UUID_MODE_SECURE = 1,
    STATUS_OK = 0,
    STATUS_NANOS_OUT_OF_RANGE = 1,
    STATUS_TIMESTAMP_TOO_LARGE = 2,
    STATUS_RANDOM_FAILURE = 3,
    STATUS_INVALID_MODE = 4,
};

int c_uuid_v7_build_default(int mode, uint64_t *hi, uint64_t *lo);
int c_uuid_v7_build_parts(
    uint64_t timestamp_s,
    int has_timestamp,
    uint64_t nanos,
    int has_nanos,
    int mode,
    uint64_t *hi,
    uint64_t *lo
);
void c_uuid_v7_reseed(void);
void c_uuid_v7_pack_bytes(uint64_t hi, uint64_t lo, unsigned char bytes[16]);
void c_uuid_v7_pack_bytes_le(uint64_t hi, uint64_t lo, unsigned char bytes[16]);
void c_uuid_v7_format_hex(uint64_t hi, uint64_t lo, char out[32]);
void c_uuid_v7_format_hyphenated(uint64_t hi, uint64_t lo, char out[36]);
uint64_t c_uuid_v7_timestamp_ms(uint64_t hi);
uint32_t c_uuid_v7_time_low(uint64_t hi);
uint16_t c_uuid_v7_time_mid(uint64_t hi);
uint16_t c_uuid_v7_time_hi_version(uint64_t hi);
uint8_t c_uuid_v7_clock_seq_hi_variant(uint64_t lo);
uint8_t c_uuid_v7_clock_seq_low(uint64_t lo);
uint16_t c_uuid_v7_clock_seq(uint64_t lo);
uint64_t c_uuid_v7_node(uint64_t lo);
int c_uuid_v7_compare(uint64_t left_hi, uint64_t left_lo, uint64_t right_hi, uint64_t right_lo);
HPy_ssize_t c_uuid_v7_hash(uint64_t hi, uint64_t lo);

static HPyGlobal UUID_TYPE;

static HPy
dup_singleton(HPyContext *ctx, HPy handle)
{
    return HPy_Dup(ctx, handle);
}

static int
set_core_error(HPyContext *ctx, int status)
{
    switch (status) {
        case STATUS_OK:
            return 0;
        case STATUS_NANOS_OUT_OF_RANGE:
            HPyErr_SetString(ctx, ctx->h_ValueError, "nanos must be in range 0..999999999");
            return -1;
        case STATUS_TIMESTAMP_TOO_LARGE:
            HPyErr_SetString(ctx, ctx->h_ValueError, "timestamp is too large");
            return -1;
        case STATUS_RANDOM_FAILURE:
            HPyErr_SetString(ctx, ctx->h_OSError, "unable to seed UUIDv7 generator");
            return -1;
        case STATUS_INVALID_MODE:
        default:
            HPyErr_SetString(ctx, ctx->h_ValueError, "mode must be 'fast' or 'secure'");
            return -1;
    }
}

static int
parse_optional_u64(HPyContext *ctx, HPy value, const char *name, int *present, uint64_t *out)
{
    unsigned long long parsed;

    if (HPy_IsNull(value) || HPy_Is(ctx, value, ctx->h_None)) {
        *present = 0;
        *out = 0;
        return 0;
    }

    parsed = HPyLong_AsUnsignedLongLong(ctx, value);
    if (HPyErr_Occurred(ctx)) {
        char message[64];

        HPyErr_Clear(ctx);
        snprintf(message, sizeof(message), "%s must be a non-negative int or None", name);
        HPyErr_SetString(ctx, ctx->h_TypeError, message);
        return -1;
    }

    *present = 1;
    *out = (uint64_t)parsed;
    return 0;
}

static int
parse_mode(HPyContext *ctx, HPy value, int *mode)
{
    const char *text;

    if (HPy_IsNull(value) || HPy_Is(ctx, value, ctx->h_None)) {
        *mode = UUID_MODE_FAST;
        return 0;
    }

    if (!HPyUnicode_Check(ctx, value)) {
        HPyErr_SetString(ctx, ctx->h_TypeError, "mode must be 'fast', 'secure', or None");
        return -1;
    }

    text = HPyUnicode_AsUTF8AndSize(ctx, value, NULL);
    if (text == NULL) {
        return -1;
    }

    if (strcmp(text, "fast") == 0) {
        *mode = UUID_MODE_FAST;
        return 0;
    }
    if (strcmp(text, "secure") == 0) {
        *mode = UUID_MODE_SECURE;
        return 0;
    }

    HPyErr_SetString(ctx, ctx->h_ValueError, "mode must be 'fast' or 'secure'");
    return -1;
}

static HPy
uuid_type_load(HPyContext *ctx)
{
    return HPyGlobal_Load(ctx, UUID_TYPE);
}

static HPy
uuid_new(HPyContext *ctx, uint64_t hi, uint64_t lo)
{
    HPy uuid_type = uuid_type_load(ctx);
    HPy obj;
    UUIDObject *payload;

    if (HPy_IsNull(uuid_type)) {
        return HPy_NULL;
    }

    obj = HPy_New(ctx, uuid_type, &payload);
    HPy_Close(ctx, uuid_type);
    if (HPy_IsNull(obj)) {
        return HPy_NULL;
    }

    payload->hi = hi;
    payload->lo = lo;
    return obj;
}

static HPy
uuid_string_from_words(HPyContext *ctx, uint64_t hi, uint64_t lo)
{
    char text[37];

    c_uuid_v7_format_hyphenated(hi, lo, text);
    text[36] = '\0';
    return HPyUnicode_FromString(ctx, text);
}

static HPy
uuid_hex_from_words(HPyContext *ctx, uint64_t hi, uint64_t lo)
{
    char text[33];

    c_uuid_v7_format_hex(hi, lo, text);
    text[32] = '\0';
    return HPyUnicode_FromString(ctx, text);
}

static HPy
uuid_int_from_words(HPyContext *ctx, uint64_t hi, uint64_t lo)
{
    HPy high = HPyLong_FromUnsignedLongLong(ctx, hi);
    HPy bits = HPy_NULL;
    HPy shifted = HPy_NULL;
    HPy low = HPy_NULL;
    HPy result = HPy_NULL;

    if (HPy_IsNull(high)) {
        return HPy_NULL;
    }

    bits = HPyLong_FromLong(ctx, 64);
    if (HPy_IsNull(bits)) {
        HPy_Close(ctx, high);
        return HPy_NULL;
    }

    shifted = HPy_Lshift(ctx, high, bits);
    if (HPy_IsNull(shifted)) {
        HPy_Close(ctx, bits);
        HPy_Close(ctx, high);
        return HPy_NULL;
    }

    low = HPyLong_FromUnsignedLongLong(ctx, lo);
    if (HPy_IsNull(low)) {
        HPy_Close(ctx, shifted);
        HPy_Close(ctx, bits);
        HPy_Close(ctx, high);
        return HPy_NULL;
    }

    result = HPy_Or(ctx, shifted, low);
    HPy_Close(ctx, low);
    HPy_Close(ctx, shifted);
    HPy_Close(ctx, bits);
    HPy_Close(ctx, high);
    return result;
}

HPyDef_SLOT(UUID_repr, HPy_tp_repr)
static HPy
UUID_repr_impl(HPyContext *ctx, HPy self)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
    char text[37];
    char repr[45];

    c_uuid_v7_format_hyphenated(uuid->hi, uuid->lo, text);
    text[36] = '\0';
    snprintf(repr, sizeof(repr), "UUID('%s')", text);
    return HPyUnicode_FromString(ctx, repr);
}

HPyDef_SLOT(UUID_str, HPy_tp_str)
static HPy
UUID_str_impl(HPyContext *ctx, HPy self)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    return uuid_string_from_words(ctx, uuid->hi, uuid->lo);
}

HPyDef_SLOT(UUID_hash, HPy_tp_hash)
static HPy_ssize_t
UUID_hash_impl(HPyContext *ctx, HPy self)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)ctx;
    return c_uuid_v7_hash(uuid->hi, uuid->lo);
}

HPyDef_SLOT(UUID_int, HPy_nb_int)
static HPy
UUID_int_impl(HPyContext *ctx, HPy self)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    return uuid_int_from_words(ctx, uuid->hi, uuid->lo);
}

HPyDef_SLOT(UUID_richcompare, HPy_tp_richcompare)
static HPy
UUID_richcompare_impl(HPyContext *ctx, HPy self, HPy other, HPy_RichCmpOp op)
{
    HPy uuid_type = uuid_type_load(ctx);
    UUIDObject *left;
    UUIDObject *right;
    int cmp;
    int result;

    if (HPy_IsNull(uuid_type)) {
        return HPy_NULL;
    }
    if (!HPy_TypeCheck(ctx, other, uuid_type)) {
        HPy_Close(ctx, uuid_type);
        return dup_singleton(ctx, ctx->h_NotImplemented);
    }
    HPy_Close(ctx, uuid_type);

    left = UUIDObject_AsStruct(ctx, self);
    right = UUIDObject_AsStruct(ctx, other);
    cmp = c_uuid_v7_compare(left->hi, left->lo, right->hi, right->lo);

    switch (op) {
        case HPy_LT:
            result = cmp < 0;
            break;
        case HPy_LE:
            result = cmp <= 0;
            break;
        case HPy_EQ:
            result = cmp == 0;
            break;
        case HPy_NE:
            result = cmp != 0;
            break;
        case HPy_GT:
            result = cmp > 0;
            break;
        case HPy_GE:
            result = cmp >= 0;
            break;
        default:
            return dup_singleton(ctx, ctx->h_NotImplemented);
    }

    return dup_singleton(ctx, result ? ctx->h_True : ctx->h_False);
}

HPyDef_GET(UUID_bytes, "bytes")
static HPy
UUID_bytes_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
    unsigned char bytes[16];

    (void)closure;
    c_uuid_v7_pack_bytes(uuid->hi, uuid->lo, bytes);
    return HPyBytes_FromStringAndSize(ctx, (const char *)bytes, 16);
}

HPyDef_GET(UUID_bytes_le, "bytes_le")
static HPy
UUID_bytes_le_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
    unsigned char bytes[16];

    (void)closure;
    c_uuid_v7_pack_bytes_le(uuid->hi, uuid->lo, bytes);
    return HPyBytes_FromStringAndSize(ctx, (const char *)bytes, 16);
}

HPyDef_GET(UUID_clock_seq, "clock_seq")
static HPy
UUID_clock_seq_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_clock_seq(uuid->lo));
}

HPyDef_GET(UUID_clock_seq_hi_variant, "clock_seq_hi_variant")
static HPy
UUID_clock_seq_hi_variant_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_clock_seq_hi_variant(uuid->lo));
}

HPyDef_GET(UUID_clock_seq_low, "clock_seq_low")
static HPy
UUID_clock_seq_low_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_clock_seq_low(uuid->lo));
}

HPyDef_GET(UUID_fields, "fields")
static HPy
UUID_fields_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
    HPy items[6] = {
        HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_low(uuid->hi)),
        HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_mid(uuid->hi)),
        HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_hi_version(uuid->hi)),
        HPyLong_FromUnsignedLong(ctx, c_uuid_v7_clock_seq_hi_variant(uuid->lo)),
        HPyLong_FromUnsignedLong(ctx, c_uuid_v7_clock_seq_low(uuid->lo)),
        HPyLong_FromUnsignedLongLong(ctx, c_uuid_v7_node(uuid->lo)),
    };
    HPy tuple;
    size_t i;

    (void)closure;
    for (i = 0; i < 6; i++) {
        if (HPy_IsNull(items[i])) {
            while (i > 0) {
                i--;
                HPy_Close(ctx, items[i]);
            }
            return HPy_NULL;
        }
    }

    tuple = HPyTuple_Pack(ctx, 6, items[0], items[1], items[2], items[3], items[4], items[5]);
    for (i = 0; i < 6; i++) {
        HPy_Close(ctx, items[i]);
    }
    return tuple;
}

HPyDef_GET(UUID_hex, "hex")
static HPy
UUID_hex_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return uuid_hex_from_words(ctx, uuid->hi, uuid->lo);
}

HPyDef_GET(UUID_int_attr, "int")
static HPy
UUID_int_attr_get(HPyContext *ctx, HPy self, void *closure)
{
    (void)closure;
    return UUID_int_impl(ctx, self);
}

HPyDef_GET(UUID_node, "node")
static HPy
UUID_node_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLongLong(ctx, c_uuid_v7_node(uuid->lo));
}

HPyDef_GET(UUID_time, "time")
static HPy
UUID_time_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLongLong(ctx, c_uuid_v7_timestamp_ms(uuid->hi));
}

HPyDef_GET(UUID_time_hi_version, "time_hi_version")
static HPy
UUID_time_hi_version_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_hi_version(uuid->hi));
}

HPyDef_GET(UUID_time_low, "time_low")
static HPy
UUID_time_low_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_low(uuid->hi));
}

HPyDef_GET(UUID_time_mid, "time_mid")
static HPy
UUID_time_mid_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);

    (void)closure;
    return HPyLong_FromUnsignedLong(ctx, c_uuid_v7_time_mid(uuid->hi));
}

HPyDef_GET(UUID_timestamp, "timestamp")
static HPy
UUID_timestamp_get(HPyContext *ctx, HPy self, void *closure)
{
    (void)closure;
    return UUID_time_get(ctx, self, NULL);
}

HPyDef_GET(UUID_urn, "urn")
static HPy
UUID_urn_get(HPyContext *ctx, HPy self, void *closure)
{
    UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
    char text[37];
    char urn[46];

    (void)closure;
    c_uuid_v7_format_hyphenated(uuid->hi, uuid->lo, text);
    text[36] = '\0';
    snprintf(urn, sizeof(urn), "urn:uuid:%s", text);
    return HPyUnicode_FromString(ctx, urn);
}

static HPyDef *UUID_defines[] = {
    &UUID_repr,
    &UUID_str,
    &UUID_hash,
    &UUID_int,
    &UUID_richcompare,
    &UUID_bytes,
    &UUID_bytes_le,
    &UUID_clock_seq,
    &UUID_clock_seq_hi_variant,
    &UUID_clock_seq_low,
    &UUID_fields,
    &UUID_hex,
    &UUID_int_attr,
    &UUID_node,
    &UUID_time,
    &UUID_time_hi_version,
    &UUID_time_low,
    &UUID_time_mid,
    &UUID_timestamp,
    &UUID_urn,
    NULL,
};

static HPyType_Spec UUID_spec = {
    .name = "c_uuid_v7.UUID",
    .basicsize = sizeof(UUIDObject),
    .builtin_shape = UUIDObject_SHAPE,
    .flags = HPy_TPFLAGS_DEFAULT,
    .defines = UUID_defines,
};

HPyDef_METH(_uuid7, "_uuid7", HPyFunc_KEYWORDS, .doc = "Generate a UUIDv7 object.")
static HPy
_uuid7_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs, HPy kwnames)
{
    HPy timestamp_obj = HPy_NULL;
    HPy nanos_obj = HPy_NULL;
    HPy mode_obj = HPy_NULL;
    int has_timestamp = 0;
    int has_nanos = 0;
    int mode = UUID_MODE_FAST;
    uint64_t timestamp_s = 0;
    uint64_t nanos = 0;
    uint64_t hi = 0;
    uint64_t lo = 0;
    HPy_ssize_t nkw = HPy_IsNull(kwnames) ? 0 : HPy_Length(ctx, kwnames);
    HPy_ssize_t i;

    (void)self;

    if (!HPy_IsNull(kwnames) && nkw < 0) {
        return HPy_NULL;
    }
    if (nargs > 3) {
        HPyErr_SetString(ctx, ctx->h_TypeError, "uuid7() takes at most 3 positional arguments");
        return HPy_NULL;
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

    for (i = 0; i < nkw; i++) {
        HPy key = HPy_GetItem_i(ctx, kwnames, i);
        const char *name;

        if (HPy_IsNull(key)) {
            return HPy_NULL;
        }

        name = HPyUnicode_AsUTF8AndSize(ctx, key, NULL);
        if (name == NULL) {
            HPy_Close(ctx, key);
            return HPy_NULL;
        }

        if (strcmp(name, "timestamp") == 0) {
            timestamp_obj = args[nargs + (size_t)i];
        } else if (strcmp(name, "nanos") == 0) {
            nanos_obj = args[nargs + (size_t)i];
        } else if (strcmp(name, "mode") == 0) {
            mode_obj = args[nargs + (size_t)i];
        } else {
            char message[128];

            snprintf(message, sizeof(message), "uuid7() got an unexpected keyword argument '%s'", name);
            HPy_Close(ctx, key);
            HPyErr_SetString(ctx, ctx->h_TypeError, message);
            return HPy_NULL;
        }

        HPy_Close(ctx, key);
    }

    if (parse_optional_u64(ctx, timestamp_obj, "timestamp", &has_timestamp, &timestamp_s) < 0) {
        return HPy_NULL;
    }
    if (parse_optional_u64(ctx, nanos_obj, "nanos", &has_nanos, &nanos) < 0) {
        return HPy_NULL;
    }
    if (parse_mode(ctx, mode_obj, &mode) < 0) {
        return HPy_NULL;
    }

    if (!has_timestamp && !has_nanos) {
        if (set_core_error(ctx, c_uuid_v7_build_default(mode, &hi, &lo)) < 0) {
            return HPy_NULL;
        }
    } else if (set_core_error(
                   ctx,
                   c_uuid_v7_build_parts(timestamp_s, has_timestamp, nanos, has_nanos, mode, &hi, &lo)
               ) < 0) {
        return HPy_NULL;
    }

    return uuid_new(ctx, hi, lo);
}

HPyDef_METH(_reseed_rng, "_reseed_rng", HPyFunc_NOARGS, .doc = "Reseed the internal RNG state.")
static HPy
_reseed_rng_impl(HPyContext *ctx, HPy self)
{
    (void)self;
    c_uuid_v7_reseed();
    return dup_singleton(ctx, ctx->h_None);
}

HPyDef_SLOT(module_exec, HPy_mod_exec)
static int
module_exec_impl(HPyContext *ctx, HPy mod)
{
    HPy uuid_type = HPyType_FromSpec(ctx, &UUID_spec, NULL);

    if (HPy_IsNull(uuid_type)) {
        return -1;
    }
    HPyGlobal_Store(ctx, &UUID_TYPE, uuid_type);
    if (HPyErr_Occurred(ctx)) {
        HPy_Close(ctx, uuid_type);
        return -1;
    }
    if (HPy_SetAttr_s(ctx, mod, "_UUID", uuid_type) < 0) {
        HPy_Close(ctx, uuid_type);
        return -1;
    }

    HPy_Close(ctx, uuid_type);
    return 0;
}

static HPyDef *module_defines[] = {
    &module_exec,
    &_uuid7,
    &_reseed_rng,
    NULL,
};

static HPyModuleDef moduledef = {
    .doc = "HPy UUIDv7 wrapper around the Zig core.",
    .size = 0,
    .defines = module_defines,
};

HPy_MODINIT(_core, moduledef)
