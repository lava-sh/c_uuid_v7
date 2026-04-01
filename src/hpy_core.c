#include "core.h"

#include <hpy.h>

#include <stdint.h>
#include <string.h>

typedef struct {
  uint64_t hi;
  uint64_t lo;
} UUIDObject;

HPyType_HELPERS(UUIDObject)

    static void uuid_pack_bytes(uint64_t hi, uint64_t lo,
                                unsigned char bytes[16]) {
  for (int i = 0; i < 8; i++) {
    bytes[i] = (unsigned char)(hi >> (56 - (i * 8)));
    bytes[8 + i] = (unsigned char)(lo >> (56 - (i * 8)));
  }
}

static void uuid_format_hex(char *out, uint64_t hi, uint64_t lo,
                            int with_hyphens) {
  static const char digits[] = "0123456789abcdef";
  unsigned char bytes[16];
  int j = 0;

  uuid_pack_bytes(hi, lo, bytes);
  for (int i = 0; i < 16; i++) {
    if (with_hyphens && (i == 4 || i == 6 || i == 8 || i == 10)) {
      out[j++] = '-';
    }
    out[j++] = digits[bytes[i] >> 4];
    out[j++] = digits[bytes[i] & 0x0F];
  }
}

static HPy uuid_int_from_words(HPyContext *ctx, uint64_t hi, uint64_t lo) {
  HPy hi_obj = HPy_NULL;
  HPy shift_bits = HPy_NULL;
  HPy shifted = HPy_NULL;
  HPy lo_obj = HPy_NULL;
  HPy result = HPy_NULL;

  hi_obj = HPyLong_FromUInt64_t(ctx, hi);
  if (HPy_IsNull(hi_obj)) {
    goto done;
  }
  shift_bits = HPyLong_FromLong(ctx, 64);
  if (HPy_IsNull(shift_bits)) {
    goto done;
  }
  shifted = HPy_Lshift(ctx, hi_obj, shift_bits);
  if (HPy_IsNull(shifted)) {
    goto done;
  }
  lo_obj = HPyLong_FromUInt64_t(ctx, lo);
  if (HPy_IsNull(lo_obj)) {
    goto done;
  }
  result = HPy_Or(ctx, shifted, lo_obj);

done:
  HPy_Close(ctx, lo_obj);
  HPy_Close(ctx, shifted);
  HPy_Close(ctx, shift_bits);
  HPy_Close(ctx, hi_obj);
  return result;
}

static HPy make_uuid(HPyContext *ctx, HPy module, uint64_t hi, uint64_t lo) {
  HPy type = HPy_NULL;
  HPy result = HPy_NULL;
  UUIDObject *uuid = NULL;

  type = HPy_GetAttr_s(ctx, module, "_UUID");
  if (HPy_IsNull(type)) {
    goto done;
  }

  result = HPy_New(ctx, type, (void **)&uuid);
  if (HPy_IsNull(result)) {
    goto done;
  }

  uuid->hi = hi;
  uuid->lo = lo;

done:
  HPy_Close(ctx, type);
  return result;
}

static int parse_optional_u64(HPyContext *ctx, HPy value, const char *name,
                              uint64_t *out, int *has_value) {
  if (HPy_IsNull(value) || HPy_Is(ctx, value, ctx->h_None)) {
    *has_value = 0;
    return 0;
  }

  *out = HPyLong_AsUInt64_t(ctx, value);
  if (!HPyErr_Occurred(ctx)) {
    *has_value = 1;
    return 0;
  }

  HPyErr_Clear(ctx);
  if (strcmp(name, "timestamp") == 0) {
    HPyErr_SetString(ctx, ctx->h_TypeError,
                     "timestamp must be a non-negative int or None");
  } else {
    HPyErr_SetString(ctx, ctx->h_TypeError,
                     "nanos must be a non-negative int or None");
  }
  return -1;
}

static int parse_mode(HPyContext *ctx, HPy value, int *mode) {
  const char *text = NULL;
  HPy_ssize_t size = 0;

  if (HPy_IsNull(value) || HPy_Is(ctx, value, ctx->h_None)) {
    *mode = C_UUID_V7_MODE_FAST;
    return 0;
  }

  if (!HPyUnicode_Check(ctx, value)) {
    HPyErr_SetString(ctx, ctx->h_TypeError,
                     "mode must be 'fast', 'secure', or None");
    return -1;
  }

  text = HPyUnicode_AsUTF8AndSize(ctx, value, &size);
  if (text == NULL) {
    return -1;
  }
  if (size == 4 && memcmp(text, "fast", 4) == 0) {
    *mode = C_UUID_V7_MODE_FAST;
    return 0;
  }
  if (size == 6 && memcmp(text, "secure", 6) == 0) {
    *mode = C_UUID_V7_MODE_SECURE;
    return 0;
  }

  HPyErr_SetString(ctx, ctx->h_ValueError, "mode must be 'fast' or 'secure'");
  return -1;
}

static int raise_core_error(HPyContext *ctx, int error_code) {
  if (error_code == C_UUID_V7_OK) {
    return 0;
  }
  if (error_code == C_UUID_V7_ERR_OS) {
    HPyErr_SetString(ctx, ctx->h_OSError, "unable to seed UUIDv7 generator");
    return -1;
  }
  if (error_code == C_UUID_V7_ERR_NANOS_RANGE) {
    HPyErr_SetString(ctx, ctx->h_ValueError,
                     "nanos must be in range 0..999999999");
    return -1;
  }

  HPyErr_SetString(ctx, ctx->h_ValueError, "timestamp is too large");
  return -1;
}

HPyDef_SLOT(uuid_str, HPy_tp_str) static HPy
    uuid_str_impl(HPyContext *ctx, HPy self) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  char out[37];

  uuid_format_hex(out, uuid->hi, uuid->lo, 1);
  out[36] = '\0';
  return HPyUnicode_FromString(ctx, out);
}

HPyDef_SLOT(uuid_repr, HPy_tp_repr) static HPy
    uuid_repr_impl(HPyContext *ctx, HPy self) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  char out[45];

  out[0] = 'U';
  out[1] = 'U';
  out[2] = 'I';
  out[3] = 'D';
  out[4] = '(';
  out[5] = '\'';
  uuid_format_hex(out + 6, uuid->hi, uuid->lo, 1);
  out[42] = '\'';
  out[43] = ')';
  out[44] = '\0';
  return HPyUnicode_FromString(ctx, out);
}

HPyDef_GET(uuid_hex, "hex") static HPy
    uuid_hex_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  char out[33];
  (void)closure;

  uuid_format_hex(out, uuid->hi, uuid->lo, 0);
  out[32] = '\0';
  return HPyUnicode_FromString(ctx, out);
}

HPyDef_GET(uuid_bytes, "bytes") static HPy
    uuid_bytes_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  unsigned char bytes[16];
  (void)closure;

  uuid_pack_bytes(uuid->hi, uuid->lo, bytes);
  return HPyBytes_FromStringAndSize(ctx, (const char *)bytes, 16);
}

HPyDef_GET(uuid_bytes_le, "bytes_le") static HPy
    uuid_bytes_le_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  unsigned char bytes[16];
  unsigned char reordered[16];
  (void)closure;

  uuid_pack_bytes(uuid->hi, uuid->lo, bytes);
  reordered[0] = bytes[3];
  reordered[1] = bytes[2];
  reordered[2] = bytes[1];
  reordered[3] = bytes[0];
  reordered[4] = bytes[5];
  reordered[5] = bytes[4];
  reordered[6] = bytes[7];
  reordered[7] = bytes[6];
  for (int i = 8; i < 16; i++) {
    reordered[i] = bytes[i];
  }
  return HPyBytes_FromStringAndSize(ctx, (const char *)reordered, 16);
}

HPyDef_GET(uuid_timestamp, "timestamp") static HPy
    uuid_timestamp_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt64_t(ctx, uuid->hi >> 16);
}

HPyDef_GET(uuid_time, "time") static HPy
    uuid_time_get(HPyContext *ctx, HPy self, void *closure) {
  return uuid_timestamp_get(ctx, self, closure);
}

HPyDef_GET(uuid_int, "int") static HPy
    uuid_int_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return uuid_int_from_words(ctx, uuid->hi, uuid->lo);
}

HPyDef_SLOT(uuid_nb_int, HPy_nb_int) static HPy
    uuid_nb_int_impl(HPyContext *ctx, HPy self) {
  return uuid_int_get(ctx, self, NULL);
}

HPyDef_GET(uuid_time_low, "time_low") static HPy
    uuid_time_low_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->hi >> 32));
}

HPyDef_GET(uuid_time_mid, "time_mid") static HPy
    uuid_time_mid_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (uint32_t)((uuid->hi >> 16) & 0xFFFF));
}

HPyDef_GET(uuid_time_hi_version, "time_hi_version") static HPy
    uuid_time_hi_version_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->hi & 0xFFFF));
}

HPyDef_GET(uuid_clock_seq_hi_variant, "clock_seq_hi_variant") static HPy
    uuid_clock_seq_hi_variant_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->lo >> 56));
}

HPyDef_GET(uuid_clock_seq_low, "clock_seq_low") static HPy
    uuid_clock_seq_low_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (uint32_t)((uuid->lo >> 48) & 0xFF));
}

HPyDef_GET(uuid_clock_seq, "clock_seq") static HPy
    uuid_clock_seq_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  uint32_t high = (uint32_t)((uuid->lo >> 56) & 0x3F);
  uint32_t low = (uint32_t)((uuid->lo >> 48) & 0xFF);
  (void)closure;
  return HPyLong_FromUInt32_t(ctx, (high << 8) | low);
}

HPyDef_GET(uuid_node, "node") static HPy
    uuid_node_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  (void)closure;
  return HPyLong_FromUInt64_t(ctx, uuid->lo & 0xFFFFFFFFFFFFULL);
}

HPyDef_GET(uuid_fields, "fields") static HPy
    uuid_fields_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  HPy items[6] = {HPy_NULL, HPy_NULL, HPy_NULL, HPy_NULL, HPy_NULL, HPy_NULL};
  HPy result = HPy_NULL;
  (void)closure;

  items[0] = HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->hi >> 32));
  items[1] = HPyLong_FromUInt32_t(ctx, (uint32_t)((uuid->hi >> 16) & 0xFFFF));
  items[2] = HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->hi & 0xFFFF));
  items[3] = HPyLong_FromUInt32_t(ctx, (uint32_t)(uuid->lo >> 56));
  items[4] = HPyLong_FromUInt32_t(ctx, (uint32_t)((uuid->lo >> 48) & 0xFF));
  items[5] = HPyLong_FromUInt64_t(ctx, uuid->lo & 0xFFFFFFFFFFFFULL);
  for (int i = 0; i < 6; i++) {
    if (HPy_IsNull(items[i])) {
      goto done;
    }
  }

  result = HPyTuple_FromArray(ctx, items, 6);

done:
  for (int i = 0; i < 6; i++) {
    HPy_Close(ctx, items[i]);
  }
  return result;
}

HPyDef_GET(uuid_urn, "urn") static HPy
    uuid_urn_get(HPyContext *ctx, HPy self, void *closure) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  char out[46];
  (void)closure;

  out[0] = 'u';
  out[1] = 'r';
  out[2] = 'n';
  out[3] = ':';
  out[4] = 'u';
  out[5] = 'u';
  out[6] = 'i';
  out[7] = 'd';
  out[8] = ':';
  uuid_format_hex(out + 9, uuid->hi, uuid->lo, 1);
  out[45] = '\0';
  return HPyUnicode_FromString(ctx, out);
}

HPyDef_SLOT(uuid_hash, HPy_tp_hash) static HPy_hash_t
    uuid_hash_impl(HPyContext *ctx, HPy self) {
  UUIDObject *uuid = UUIDObject_AsStruct(ctx, self);
  uint64_t mixed = uuid->hi ^ (uuid->hi >> 32) ^ uuid->lo ^ (uuid->lo >> 32);
  HPy_hash_t hash;
  (void)ctx;

  if (sizeof(HPy_hash_t) == 4) {
    hash = (HPy_hash_t)(int32_t)(uint32_t)(mixed ^ (mixed >> 32));
  } else {
    hash = (HPy_hash_t)(int64_t)mixed;
  }

  return hash == -1 ? -2 : hash;
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

HPyDef_SLOT(uuid_richcompare, HPy_tp_richcompare) static HPy
    uuid_richcompare_impl(HPyContext *ctx, HPy self, HPy other,
                          HPy_RichCmpOp op) {
  HPy type = HPy_NULL;
  UUIDObject *left = NULL;
  UUIDObject *right = NULL;
  int cmp = 0;
  int truth = 0;

  type = HPy_Type(ctx, self);
  if (HPy_IsNull(type)) {
    return HPy_NULL;
  }
  if (!HPy_TypeCheck(ctx, other, type)) {
    HPy_Close(ctx, type);
    return HPy_Dup(ctx, ctx->h_NotImplemented);
  }
  HPy_Close(ctx, type);

  left = UUIDObject_AsStruct(ctx, self);
  right = UUIDObject_AsStruct(ctx, other);
  cmp = uuid_compare(left, right);

  if (op == HPy_LT) {
    truth = cmp < 0;
  } else if (op == HPy_LE) {
    truth = cmp <= 0;
  } else if (op == HPy_EQ) {
    truth = cmp == 0;
  } else if (op == HPy_NE) {
    truth = cmp != 0;
  } else if (op == HPy_GT) {
    truth = cmp > 0;
  } else if (op == HPy_GE) {
    truth = cmp >= 0;
  } else {
    return HPy_Dup(ctx, ctx->h_NotImplemented);
  }

  return HPyBool_FromBool(ctx, truth);
}

HPyDef_METH(module_copy_uuid, "_copy_uuid", HPyFunc_O) static HPy
    module_copy_uuid_impl(HPyContext *ctx, HPy self, HPy value) {
  (void)self;
  return HPy_Dup(ctx, value);
}

HPyDef_METH(module_deepcopy_uuid, "_deepcopy_uuid", HPyFunc_VARARGS) static HPy
    module_deepcopy_uuid_impl(HPyContext *ctx, HPy self, const HPy *args,
                              size_t nargs) {
  (void)self;
  if (nargs == 0) {
    HPyErr_SetString(ctx, ctx->h_TypeError, "expected UUID argument");
    return HPy_NULL;
  }
  return HPy_Dup(ctx, args[0]);
}

static int install_copy_dispatch(HPyContext *ctx, HPy module, HPy type) {
  HPy copy_module = HPy_NULL;
  HPy copy_dispatch = HPy_NULL;
  HPy deepcopy_dispatch = HPy_NULL;
  HPy copy_func = HPy_NULL;
  HPy deepcopy_func = HPy_NULL;
  int status = -1;

  copy_module = HPyImport_ImportModule(ctx, "copy");
  if (HPy_IsNull(copy_module)) {
    goto done;
  }
  copy_dispatch = HPy_GetAttr_s(ctx, copy_module, "_copy_dispatch");
  if (HPy_IsNull(copy_dispatch)) {
    goto done;
  }
  deepcopy_dispatch = HPy_GetAttr_s(ctx, copy_module, "_deepcopy_dispatch");
  if (HPy_IsNull(deepcopy_dispatch)) {
    goto done;
  }
  copy_func = HPy_GetAttr_s(ctx, module, "_copy_uuid");
  if (HPy_IsNull(copy_func)) {
    goto done;
  }
  deepcopy_func = HPy_GetAttr_s(ctx, module, "_deepcopy_uuid");
  if (HPy_IsNull(deepcopy_func)) {
    goto done;
  }
  if (HPy_SetItem(ctx, copy_dispatch, type, copy_func) < 0) {
    goto done;
  }
  if (HPy_SetItem(ctx, deepcopy_dispatch, type, deepcopy_func) < 0) {
    goto done;
  }

  status = 0;

done:
  HPy_Close(ctx, deepcopy_func);
  HPy_Close(ctx, copy_func);
  HPy_Close(ctx, deepcopy_dispatch);
  HPy_Close(ctx, copy_dispatch);
  HPy_Close(ctx, copy_module);
  return status;
}

HPyDef_METH(module_uuid7, "_uuid7", HPyFunc_KEYWORDS) static HPy
    module_uuid7_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs,
                      HPy kwnames) {
  static const char *keywords[] = {"timestamp", "nanos", "mode", NULL};
  HPyTracker tracker = {0};
  HPy timestamp_obj = HPy_NULL;
  HPy nanos_obj = HPy_NULL;
  HPy mode_obj = HPy_NULL;
  uint64_t timestamp = 0;
  uint64_t nanos = 0;
  int has_timestamp = 0;
  int has_nanos = 0;
  int mode = C_UUID_V7_MODE_FAST;
  c_uuid_v7_uuid_words words;
  HPy result = HPy_NULL;

  if (!HPyArg_ParseKeywords(ctx, &tracker, args, nargs, kwnames, "|OOO:_uuid7",
                            keywords, &timestamp_obj, &nanos_obj, &mode_obj)) {
    return HPy_NULL;
  }

  if (parse_optional_u64(ctx, timestamp_obj, "timestamp", &timestamp,
                         &has_timestamp) < 0) {
    goto done;
  }
  if (parse_optional_u64(ctx, nanos_obj, "nanos", &nanos, &has_nanos) < 0) {
    goto done;
  }
  if (parse_mode(ctx, mode_obj, &mode) < 0) {
    goto done;
  }
  if (raise_core_error(ctx,
                       c_uuid_v7_build(timestamp, (uint8_t)has_timestamp, nanos,
                                       (uint8_t)has_nanos, mode, &words)) < 0) {
    goto done;
  }

  result = make_uuid(ctx, self, words.hi, words.lo);

done:
  if (tracker._i != 0) {
    HPyTracker_Close(ctx, tracker);
  }
  return result;
}

HPyDef_METH(module_reseed, "_reseed_rng", HPyFunc_NOARGS) static HPy
    module_reseed_impl(HPyContext *ctx, HPy self) {
  (void)self;
  c_uuid_v7_reseed();
  return HPy_Dup(ctx, ctx->h_None);
}

static HPyDef *uuid_defs[] = {
    &uuid_str,
    &uuid_repr,
    &uuid_hex,
    &uuid_bytes,
    &uuid_bytes_le,
    &uuid_timestamp,
    &uuid_time,
    &uuid_int,
    &uuid_nb_int,
    &uuid_time_low,
    &uuid_time_mid,
    &uuid_time_hi_version,
    &uuid_clock_seq_hi_variant,
    &uuid_clock_seq_low,
    &uuid_clock_seq,
    &uuid_node,
    &uuid_fields,
    &uuid_urn,
    &uuid_hash,
    &uuid_richcompare,
    NULL,
};

static HPyType_Spec uuid_spec = {
    .name = "c_uuid_v7.UUID",
    .basicsize = sizeof(UUIDObject),
    .flags = HPy_TPFLAGS_DEFAULT,
    .builtin_shape = SHAPE(UUIDObject),
    .defines = uuid_defs,
};

HPyDef_SLOT(module_exec,
            HPy_mod_exec) static int module_exec_impl(HPyContext *ctx,
                                                      HPy module) {
  HPy type = HPy_NULL;
  HPy abi = HPy_NULL;

  type = HPyType_FromSpec(ctx, &uuid_spec, NULL);
  if (HPy_IsNull(type)) {
    goto error;
  }
  if (HPy_SetAttr_s(ctx, module, "_UUID", type) < 0) {
    goto error;
  }
  if (install_copy_dispatch(ctx, module, type) < 0) {
    goto error;
  }

  abi = HPyUnicode_FromString(ctx, HPY_ABI);
  if (HPy_IsNull(abi)) {
    goto error;
  }
  if (HPy_SetAttr_s(ctx, module, "_ABI", abi) < 0) {
    goto error;
  }

  HPy_Close(ctx, abi);
  HPy_Close(ctx, type);
  return 0;

error:
  HPy_Close(ctx, abi);
  HPy_Close(ctx, type);
  return -1;
}

static HPyDef *module_defs[] = {
    &module_uuid7,         &module_reseed, &module_copy_uuid,
    &module_deepcopy_uuid, &module_exec,   NULL,
};

static HPyModuleDef module_def = {
    .doc = "Fast UUIDv7 generator backed by a Zig core and HPy bindings.",
    .size = 0,
    .defines = module_defs,
};

HPy_MODINIT(_core, module_def)
