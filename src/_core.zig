const std = @import("std");
const builtin = @import("builtin");

// Import the core UUID logic
const lib = @import("lib.zig");

// ============================================================================
// Python C API Declarations
// ============================================================================

const PyObject = opaque {};
const PyTypeObject = opaque {};
const Py_ssize_t = if (@bitSizeOf(usize) == 64) i64 else i32;
const Py_hash_t = Py_ssize_t;

// Method flags
const METH_VARARGS = 0x0001;
const METH_KEYWORDS = 0x0002;
const METH_NOARGS = 0x0004;
const METH_O = 0x0008;

// Type flags
const Py_TPFLAGS_DEFAULT = @as(c_uint, 1 << 0);
const Py_TPFLAGS_HAVE_VERSION_TAG = @as(c_uint, 1 << 8);

// Module flags
const Py_mod_exec = 3;

// Type slots
const Py_tp_dealloc = 1;
const Py_tp_repr = 3;
const Py_tp_str = 6;
const Py_tp_hash = 4;
const Py_tp_richcompare = 10;
const Py_tp_methods = 13;
const Py_tp_getset = 21;

// Rich comparison
const Py_LT = 0;
const Py_LE = 1;
const Py_EQ = 2;
const Py_NE = 3;
const Py_GT = 4;
const Py_GE = 5;

// PyMethodDef
const PyMethodDef = extern struct {
    ml_name: ?[*:0]const u8,
    ml_meth: ?*anyopaque,
    ml_flags: c_int,
    ml_doc: ?[*:0]const u8,
};

// PyGetSetDef
const PyGetSetDef = extern struct {
    name: ?[*:0]const u8,
    get: ?*anyopaque,
    set: ?*anyopaque,
    doc: ?[*:0]const u8,
    closure: ?*anyopaque,
};

// PyType_Slot
const PyType_Slot = extern struct {
    slot: c_int,
    pfunc: ?*anyopaque,
};

// PyType_Spec
const PyType_Spec = extern struct {
    name: [*:0]const u8,
    basicsize: c_int,
    itemsize: c_int,
    flags: c_uint,
    slots: [*]PyType_Slot,
};

// PyModuleDef_Base
const PyModuleDef_Base = extern struct {
    ob_refcnt: Py_ssize_t = 1,
    ob_type: ?*PyTypeObject = null,
    ob_size: Py_ssize_t = 0,
};

// PyModuleDef_Slot
const PyModuleDef_Slot = extern struct {
    slot: c_int,
    value: ?*anyopaque,
};

// PyModuleDef
const PyModuleDef = extern struct {
    m_base: PyModuleDef_Base,
    m_name: ?[*:0]const u8,
    m_doc: ?[*:0]const u8,
    m_size: Py_ssize_t,
    m_methods: [*]PyMethodDef,
    m_slots: ?[*]const PyModuleDef_Slot,
    m_traverse: ?*anyopaque,
    m_clear: ?*anyopaque,
    m_free: ?*anyopaque,
};

// Python C API functions
extern "python" fn PyModule_Create2(def: *const PyModuleDef, api_version: c_int) ?*PyObject;
extern "python" fn PyModule_AddObject(module: *PyObject, name: [*:0]const u8, value: *PyObject) c_int;
extern "python" fn PyType_FromSpec(spec: *const PyType_Spec, bases: ?*PyObject) ?*PyObject;

extern "python" fn PyLong_FromLongLong(v: i64) ?*PyObject;
extern "python" fn PyLong_FromUnsignedLongLong(v: u64) ?*PyObject;
extern "python" fn PyLong_FromLong(v: c_long) ?*PyObject;
extern "python" fn PyLong_FromUnsignedLong(v: c_ulong) ?*PyObject;

extern "python" fn PyUnicode_FromString(v: [*:0]const u8) ?*PyObject;

extern "python" fn PyBytes_FromStringAndSize(v: ?[*]const u8, len: Py_ssize_t) ?*PyObject;

extern "python" fn PyTuple_New(size: Py_ssize_t) ?*PyObject;
extern "python" fn PyTuple_SetItem(tuple: *PyObject, index: Py_ssize_t, value: *PyObject) c_int;

extern "python" fn PyBool_FromLong(v: c_long) ?*PyObject;

extern "python" fn PyErr_SetString(t: *PyObject, msg: [*:0]const u8) void;
extern "python" fn PyErr_Occurred() ?*PyObject;

extern "python" fn PyObject_RichCompare(o1: *PyObject, o2: *PyObject, op: c_int) ?*PyObject;
extern "python" fn PyObject_Hash(o: *PyObject) Py_hash_t;

extern "python" fn PyNumber_Lshift(o1: *PyObject, o2: *PyObject) ?*PyObject;
extern "python" fn PyNumber_Or(o1: *PyObject, o2: *PyObject) ?*PyObject;

extern "python" fn PyLong_Check(o: *PyObject) c_int;

extern "python" fn Py_None() *PyObject;
extern "python" fn Py_True() *PyObject;
extern "python" fn Py_False() *PyObject;
extern "python" fn Py_NotImplemented() *PyObject;

extern "python" fn PyExc_TypeError() *PyObject;
extern "python" fn PyExc_ValueError() *PyObject;
extern "python" fn PyExc_OSError() *PyObject;

extern "python" fn Py_TYPE(o: *const PyObject) *PyTypeObject;

// PyObject structure for direct access
const PyObjectHead = extern struct {
    ob_refcnt: Py_ssize_t,
    ob_type: ?*PyTypeObject,
};

// ============================================================================
// UUID Object Structure
// ============================================================================

const UUIDObject = extern struct {
    ob_base: PyObjectHead,
    hi: u64,
    lo: u64,
};

// ============================================================================
// Global Variables
// ============================================================================

var uuid_type_slots: [8]PyType_Slot = undefined;
var uuid_methods: [3]PyMethodDef = undefined;
var uuid_getset: [16]PyGetSetDef = undefined;
var module_methods: [5]PyMethodDef = undefined;
var module_def: PyModuleDef = undefined;
var uuid_type_spec: PyType_Spec = undefined;

// ============================================================================
// Initialization
// ============================================================================

fn init_types() void {
    uuid_methods = .{
        .{
            .ml_name = "__copy__",
            .ml_meth = @ptrCast(@constCast(&uuid_copy_impl)),
            .ml_flags = METH_NOARGS,
            .ml_doc = "Return a copy of the UUID object.",
        },
        .{
            .ml_name = "__deepcopy__",
            .ml_meth = @ptrCast(@constCast(&uuid_deepcopy_impl)),
            .ml_flags = METH_O,
            .ml_doc = "Return a deep copy of the UUID object.",
        },
        .{ .ml_name = null, .ml_meth = null, .ml_flags = 0, .ml_doc = null },
    };

    uuid_getset = .{
        .{ .name = "int", .get = @ptrCast(@constCast(&uuid_int_get)), .set = null, .doc = "the UUID as an integer", .closure = null },
        .{ .name = "hex", .get = @ptrCast(@constCast(&uuid_hex_get)), .set = null, .doc = "the UUID as a 32-character hexadecimal string", .closure = null },
        .{ .name = "bytes", .get = @ptrCast(@constCast(&uuid_bytes_get)), .set = null, .doc = "the UUID as a 16-byte string", .closure = null },
        .{ .name = "bytes_le", .get = @ptrCast(@constCast(&uuid_bytes_le_get)), .set = null, .doc = "the UUID as a 16-byte string with byte order swapped", .closure = null },
        .{ .name = "timestamp", .get = @ptrCast(@constCast(&uuid_timestamp_get)), .set = null, .doc = "the UUID timestamp", .closure = null },
        .{ .name = "time", .get = @ptrCast(@constCast(&uuid_time_get)), .set = null, .doc = "the UUID timestamp", .closure = null },
        .{ .name = "time_low", .get = @ptrCast(@constCast(&uuid_time_low_get)), .set = null, .doc = "the first 32 bits of the UUID", .closure = null },
        .{ .name = "time_mid", .get = @ptrCast(@constCast(&uuid_time_mid_get)), .set = null, .doc = "bits 32-47 of the UUID", .closure = null },
        .{ .name = "time_hi_version", .get = @ptrCast(@constCast(&uuid_time_hi_version_get)), .set = null, .doc = "bits 48-63 of the UUID", .closure = null },
        .{ .name = "clock_seq_hi_variant", .get = @ptrCast(@constCast(&uuid_clock_seq_hi_variant_get)), .set = null, .doc = "bits 64-71 of the UUID", .closure = null },
        .{ .name = "clock_seq_low", .get = @ptrCast(@constCast(&uuid_clock_seq_low_get)), .set = null, .doc = "bits 72-79 of the UUID", .closure = null },
        .{ .name = "clock_seq", .get = @ptrCast(@constCast(&uuid_clock_seq_get)), .set = null, .doc = "the clock sequence", .closure = null },
        .{ .name = "node", .get = @ptrCast(@constCast(&uuid_node_get)), .set = null, .doc = "the node ID", .closure = null },
        .{ .name = "fields", .get = @ptrCast(@constCast(&uuid_fields_get)), .set = null, .doc = "a tuple of the UUID fields", .closure = null },
        .{ .name = "urn", .get = @ptrCast(@constCast(&uuid_urn_get)), .set = null, .doc = "the UUID as a URN", .closure = null },
        .{ .name = null, .get = null, .set = null, .doc = null, .closure = null },
    };

    uuid_type_slots = .{
        .{ .slot = Py_tp_dealloc, .pfunc = @ptrCast(@constCast(&uuid_dealloc)) },
        .{ .slot = Py_tp_repr, .pfunc = @ptrCast(@constCast(&uuid_repr)) },
        .{ .slot = Py_tp_str, .pfunc = @ptrCast(@constCast(&uuid_str)) },
        .{ .slot = Py_tp_hash, .pfunc = @ptrCast(@constCast(&uuid_hash)) },
        .{ .slot = Py_tp_richcompare, .pfunc = @ptrCast(@constCast(&uuid_richcompare)) },
        .{ .slot = Py_tp_methods, .pfunc = @ptrCast(&uuid_methods) },
        .{ .slot = Py_tp_getset, .pfunc = @ptrCast(&uuid_getset) },
        .{ .slot = 0, .pfunc = null },
    };

    module_methods = .{
        .{
            .ml_name = "_uuid7",
            .ml_meth = @ptrCast(@constCast(&module_uuid7_impl)),
            .ml_flags = METH_KEYWORDS,
            .ml_doc = "Generate a UUIDv7.",
        },
        .{
            .ml_name = "_reseed_rng",
            .ml_meth = @ptrCast(@constCast(&module_reseed_impl)),
            .ml_flags = METH_NOARGS,
            .ml_doc = "Reseed the random number generator.",
        },
        .{
            .ml_name = "_copy_uuid",
            .ml_meth = @ptrCast(@constCast(&module_copy_uuid_impl)),
            .ml_flags = METH_O,
            .ml_doc = "Copy a UUID object.",
        },
        .{
            .ml_name = "_deepcopy_uuid",
            .ml_meth = @ptrCast(@constCast(&module_deepcopy_uuid_impl)),
            .ml_flags = METH_VARARGS,
            .ml_doc = "Deep copy a UUID object.",
        },
        .{ .ml_name = null, .ml_meth = null, .ml_flags = 0, .ml_doc = null },
    };

    module_def = .{
        .m_base = .{ .ob_refcnt = 1, .ob_type = null, .ob_size = 0 },
        .m_name = "c_uuid_v7._core",
        .m_doc = "Fast UUIDv7 generator implemented in Zig.",
        .m_size = 0,
        .m_methods = &module_methods,
        .m_slots = &[_]PyModuleDef_Slot{
            .{ .slot = Py_mod_exec, .value = @ptrCast(@constCast(&module_exec_impl)) },
            .{ .slot = 0, .value = null },
        },
        .m_traverse = null,
        .m_clear = null,
        .m_free = null,
    };

    uuid_type_spec = .{
        .name = "c_uuid_v7.UUID",
        .basicsize = @as(c_int, @intCast(@sizeOf(UUIDObject))),
        .itemsize = 0,
        .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VERSION_TAG,
        .slots = &uuid_type_slots,
    };
}

// ============================================================================
// Helper Functions
// ============================================================================

fn uuid_pack_bytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    for (0..8) |i| {
        bytes[i] = @truncate(hi >> (@as(u6, @intCast(56 -% i *% 8))));
        bytes[8 + i] = @truncate(lo >> (@as(u6, @intCast(56 -% i *% 8))));
    }
}

fn uuid_format_hex(hi: u64, lo: u64, with_hyphens: bool, out: []u8) void {
    var bytes: [16]u8 = undefined;
    uuid_pack_bytes(hi, lo, &bytes);

    const digits = "0123456789abcdef";
    var j: usize = 0;

    for (0..16) |i| {
        if (with_hyphens and (i == 4 or i == 6 or i == 8 or i == 10)) {
            out[j] = '-';
            j += 1;
        }
        out[j] = digits[bytes[i] >> 4];
        out[j + 1] = digits[bytes[i] & 0x0F];
        j += 2;
    }
}

fn uuid_int_from_words(hi: u64, lo: u64) ?*PyObject {
    const hi_obj = PyLong_FromUnsignedLongLong(hi) orelse return null;
    const shift = PyLong_FromLong(64) orelse return null;
    const shifted = PyNumber_Lshift(hi_obj, shift) orelse return null;
    const lo_obj = PyLong_FromUnsignedLongLong(lo) orelse return null;
    return PyNumber_Or(shifted, lo_obj);
}

// ============================================================================
// UUID Type Methods
// ============================================================================

fn uuid_dealloc(self: *UUIDObject)  void {
    _ = self;
}

fn uuid_str(self: *UUIDObject)  ?*PyObject {
    var buf: [37]u8 = undefined;
    uuid_format_hex(self.hi, self.lo, true, buf[0..36]);
    buf[36] = 0;
    return PyUnicode_FromString(@ptrCast(&buf));
}

fn uuid_repr(self: *UUIDObject)  ?*PyObject {
    var buf: [44]u8 = undefined;
    const prefix = "UUID('";
    const suffix = "')";
    @memcpy(buf[0..prefix.len], prefix);
    uuid_format_hex(self.hi, self.lo, true, buf[prefix.len..prefix.len + 36]);
    buf[42] = 0;
    @memcpy(buf[prefix.len + 36..prefix.len + 36 + suffix.len], suffix);
    return PyUnicode_FromString(@ptrCast(&buf));
}

fn uuid_hash(self: *UUIDObject)  Py_hash_t {
    const mixed = self.hi ^ (self.hi >> 32) ^ self.lo ^ (self.lo >> 32);
    const hash: Py_hash_t = if (@sizeOf(usize) == 4)
        @as(i32, @truncate(mixed ^ (mixed >> 32)))
    else
        @as(i64, @intCast(mixed));

    return if (hash == -1) -2 else hash;
}

fn uuid_richcompare(self: *UUIDObject, other: *PyObject, op: c_int)  ?*PyObject {
    const other_uuid: *UUIDObject = @ptrCast(@alignCast(other));

    const cmp: i32 = if (self.hi != other_uuid.hi)
        if (self.hi < other_uuid.hi) -1 else 1
    else if (self.lo != other_uuid.lo)
        if (self.lo < other_uuid.lo) -1 else 1
    else
        0;

    const truth = switch (op) {
        Py_LT => cmp < 0,
        Py_LE => cmp <= 0,
        Py_EQ => cmp == 0,
        Py_NE => cmp != 0,
        Py_GT => cmp > 0,
        Py_GE => cmp >= 0,
        else => return Py_NotImplemented(),
    };

    return PyBool_FromLong(if (truth) 1 else 0);
}

fn uuid_copy_impl(self: *UUIDObject)  ?*PyObject {
    _ = self;
    return null;
}

fn uuid_deepcopy_impl(self: *UUIDObject, memo: *PyObject)  ?*PyObject {
    _ = self;
    _ = memo;
    return null;
}

// Getters
fn uuid_int_get(self: *UUIDObject)  ?*PyObject {
    return uuid_int_from_words(self.hi, self.lo);
}

fn uuid_hex_get(self: *UUIDObject)  ?*PyObject {
    var buf: [33]u8 = undefined;
    uuid_format_hex(self.hi, self.lo, false, buf[0..32]);
    buf[32] = 0;
    return PyUnicode_FromString(@ptrCast(&buf));
}

fn uuid_bytes_get(self: *UUIDObject)  ?*PyObject {
    var bytes: [16]u8 = undefined;
    uuid_pack_bytes(self.hi, self.lo, &bytes);
    return PyBytes_FromStringAndSize(bytes[0..16], 16);
}

fn uuid_bytes_le_get(self: *UUIDObject)  ?*PyObject {
    var bytes: [16]u8 = undefined;
    var reordered: [16]u8 = undefined;
    uuid_pack_bytes(self.hi, self.lo, &bytes);

    reordered[0] = bytes[3];
    reordered[1] = bytes[2];
    reordered[2] = bytes[1];
    reordered[3] = bytes[0];
    reordered[4] = bytes[5];
    reordered[5] = bytes[4];
    reordered[6] = bytes[7];
    reordered[7] = bytes[6];
    for (8..16) |i| {
        reordered[i] = bytes[i];
    }

    return PyBytes_FromStringAndSize(reordered[0..16], 16);
}

fn uuid_timestamp_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLongLong(self.hi >> 16);
}

fn uuid_time_get(self: *UUIDObject)  ?*PyObject {
    return uuid_timestamp_get(self);
}

fn uuid_time_low_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLong(@truncate(self.hi >> 32));
}

fn uuid_time_mid_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLong(@truncate((self.hi >> 16) & 0xFFFF));
}

fn uuid_time_hi_version_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLong(@truncate(self.hi & 0xFFFF));
}

fn uuid_clock_seq_hi_variant_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLong(@truncate(self.lo >> 56));
}

fn uuid_clock_seq_low_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLong(@truncate((self.lo >> 48) & 0xFF));
}

fn uuid_clock_seq_get(self: *UUIDObject)  ?*PyObject {
    const high = @as(u32, @truncate((self.lo >> 56) & 0x3F));
    const low = @as(u32, @truncate((self.lo >> 48) & 0xFF));
    return PyLong_FromUnsignedLong((high << 8) | low);
}

fn uuid_node_get(self: *UUIDObject)  ?*PyObject {
    return PyLong_FromUnsignedLongLong(self.lo & 0xFFFFFFFFFFFF);
}

fn uuid_fields_get(self: *UUIDObject)  ?*PyObject {
    const tuple = PyTuple_New(6) orelse return null;

    const items = [_]struct { val: u64, shift: u32, mask: u64 }{
        .{ .val = self.hi, .shift = 32, .mask = 0xFFFFFFFF },
        .{ .val = self.hi, .shift = 16, .mask = 0xFFFF },
        .{ .val = self.hi, .shift = 0, .mask = 0xFFFF },
        .{ .val = self.lo, .shift = 56, .mask = 0xFF },
        .{ .val = self.lo, .shift = 48, .mask = 0xFF },
        .{ .val = self.lo, .shift = 0, .mask = 0xFFFFFFFFFFFF },
    };

    for (items, 0..) |item, i| {
        const val = (item.val >> @as(u6, @intCast(item.shift))) & item.mask;
        const item_obj = if (item.mask > 0xFFFFFFFF)
            PyLong_FromUnsignedLongLong(val)
        else
            PyLong_FromUnsignedLong(@truncate(val));

        if (item_obj) |obj| {
            if (PyTuple_SetItem(tuple, @intCast(i), obj) < 0) return null;
        } else {
            return null;
        }
    }

    return tuple;
}

fn uuid_urn_get(self: *UUIDObject)  ?*PyObject {
    var buf: [46]u8 = undefined;
    const prefix = "urn:uuid:";
    @memcpy(buf[0..prefix.len], prefix);
    uuid_format_hex(self.hi, self.lo, true, buf[prefix.len..prefix.len + 36]);
    buf[45] = 0;
    return PyUnicode_FromString(@ptrCast(buf[0..45]));
}

// ============================================================================
// Module Functions
// ============================================================================

fn module_uuid7_impl(module: ?*PyObject, args: ?*PyObject, kwargs: ?*PyObject)  ?*PyObject {
    _ = module;
    _ = args;
    _ = kwargs;

    var words: lib.UUIDWords = undefined;
    const status = lib.c_uuid_v7_build(0, 0, 0, 0, 0, &words);

    if (status != 0) {
        PyErr_SetString(PyExc_OSError(), "Failed to generate UUID");
        return null;
    }

    const uuid_type = PyType_FromSpec(&uuid_type_spec, null) orelse return null;
    const uuid_obj = @as(*UUIDObject, @ptrCast(@alignCast(uuid_type)));
    uuid_obj.hi = words.hi;
    uuid_obj.lo = words.lo;
    return @ptrCast(uuid_obj);
}

fn module_reseed_impl(module: ?*PyObject, args: ?*PyObject)  ?*PyObject {
    _ = module;
    _ = args;
    lib.c_uuid_v7_reseed();
    return Py_None();
}

fn module_copy_uuid_impl(module: ?*PyObject, args: ?*PyObject)  ?*PyObject {
    _ = module;
    return args;
}

fn module_deepcopy_uuid_impl(module: ?*PyObject, args: ?*PyObject, kwargs: ?*PyObject)  ?*PyObject {
    _ = module;
    _ = kwargs;
    return args;
}

fn module_exec_impl(module: ?*PyObject)  c_int {
    const uuid_type = PyType_FromSpec(&uuid_type_spec, null) orelse return -1;
    const mod = module orelse return -1;

    if (PyModule_AddObject(mod, "_UUID", uuid_type) < 0) return -1;

    return 0;
}

// ============================================================================
// Module Initialization
// ============================================================================

export fn PyInit__core() ?*PyObject {
    init_types();

    const module = PyModule_Create2(&module_def, 3);
    if (module == null) {
        return null;
    }

    if (module_exec_impl(module) < 0) {
        return null;
    }

    return module;
}
