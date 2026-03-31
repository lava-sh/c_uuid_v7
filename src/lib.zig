const hexpairs = @import("hexpairs.zig");
const state = @import("state.zig");
const uuid7 = @import("uuid7.zig");

const c = @import("c.zig").c;

const UUIDObject = state.UUIDObject;
const PyModuleDef_Base = @TypeOf(c.py_module_def_head_init());
const PyModuleDef_HEAD_INIT: PyModuleDef_Base = c.py_module_def_head_init();

const PyMethodDef = extern struct {
    ml_name: [*c]const u8 = null,
    ml_meth: c.PyCFunction = null,
    ml_flags: c_int = 0,
    ml_doc: [*c]const u8 = null,
};

const PyGetSetDef = extern struct {
    name: [*c]const u8 = null,
    get: c.getter = null,
    set: c.setter = null,
    doc: [*c]const u8 = null,
    closure: ?*anyopaque = null,
};

const PyModuleDef = extern struct {
    m_base: PyModuleDef_Base = PyModuleDef_HEAD_INIT,
    m_name: [*c]const u8,
    m_doc: [*c]const u8 = null,
    m_size: c.Py_ssize_t = -1,
    m_methods: [*]PyMethodDef,
    m_slots: [*c]c.struct_PyModuleDef_Slot = null,
    m_traverse: c.traverseproc = null,
    m_clear: c.inquiry = null,
    m_free: c.freefunc = null,
};

fn pyIncRef(obj: ?*c.PyObject) ?*c.PyObject {
    c.Py_IncRef(obj);
    return obj;
}

fn pyDecRef(obj: ?*c.PyObject) void {
    c.Py_DecRef(obj);
}

fn pyRefcnt(obj: *UUIDObject) usize {
    return @intCast(c.py_refcnt(@ptrCast(obj)));
}

fn noneObject() ?*c.PyObject {
    return @constCast(c.Py_None());
}

fn notImplementedObject() ?*c.PyObject {
    return @constCast(c.Py_NotImplemented());
}

fn uuidSelf(self_obj: ?*c.PyObject) *UUIDObject {
    return @ptrCast(self_obj.?);
}

fn uuidNew(hi: u64, lo: u64) ?*UUIDObject {
    if (state.runtime.reusable_uuid) |reusable_uuid| {
        if (pyRefcnt(reusable_uuid) == 1) {
            _ = pyIncRef(@ptrCast(reusable_uuid));
            reusable_uuid.hi = hi;
            reusable_uuid.lo = lo;
            return reusable_uuid;
        }
    }

    const type_object = state.runtime.uuid_type orelse return null;
    const alloc = type_object.tp_alloc orelse return null;
    const raw = alloc(type_object, 0) orelse return null;
    const obj: *UUIDObject = @ptrCast(raw);

    obj.hi = hi;
    obj.lo = lo;

    if (state.runtime.reusable_uuid) |reusable_uuid| {
        pyDecRef(@ptrCast(reusable_uuid));
    }
    state.runtime.reusable_uuid = obj;
    _ = pyIncRef(@ptrCast(obj));

    return obj;
}

fn uuidStr(self_obj: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    var out: [36]u8 = undefined;
    const self = uuidSelf(self_obj);
    var j: usize = 0;
    var shift: i32 = 56;

    while (shift >= 0) : (shift -= 8) {
        if (j == 8 or j == 13) {
            out[j] = '-';
            j += 1;
        }
        hexpairs.hexPair(&out[j], @truncate(self.hi >> @intCast(shift)));
        j += 2;
    }

    shift = 56;
    while (shift >= 0) : (shift -= 8) {
        if (j == 18 or j == 23) {
            out[j] = '-';
            j += 1;
        }
        hexpairs.hexPair(&out[j], @truncate(self.lo >> @intCast(shift)));
        j += 2;
    }

    return c.PyUnicode_FromStringAndSize(&out, out.len);
}

fn uuidRepr(self_obj: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    const text = uuidStr(self_obj);
    defer if (text != null) pyDecRef(text);

    if (text == null) {
        return null;
    }

    return c.PyUnicode_FromFormat("UUID('%U')", text);
}

fn uuidHex(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    var out: [32]u8 = undefined;
    const self = uuidSelf(self_obj);
    var j: usize = 0;
    var shift: i32 = 56;

    while (shift >= 0) : (shift -= 8) {
        hexpairs.hexPair(&out[j], @truncate(self.hi >> @intCast(shift)));
        j += 2;
    }

    shift = 56;
    while (shift >= 0) : (shift -= 8) {
        hexpairs.hexPair(&out[j], @truncate(self.lo >> @intCast(shift)));
        j += 2;
    }

    return c.PyUnicode_FromStringAndSize(&out, out.len);
}

fn uuidBytes(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    var bytes: [16]u8 = undefined;
    const self = uuidSelf(self_obj);

    uuid7.uuidPackBytes(self.hi, self.lo, &bytes);
    return c.PyBytes_FromStringAndSize(@ptrCast(&bytes), bytes.len);
}

fn uuidBytesLe(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    var bytes: [16]u8 = undefined;
    var reordered: [16]u8 = undefined;
    const self = uuidSelf(self_obj);

    uuid7.uuidPackBytes(self.hi, self.lo, &bytes);
    reordered[0] = bytes[3];
    reordered[1] = bytes[2];
    reordered[2] = bytes[1];
    reordered[3] = bytes[0];
    reordered[4] = bytes[5];
    reordered[5] = bytes[4];
    reordered[6] = bytes[7];
    reordered[7] = bytes[6];
    @memcpy(reordered[8..16], bytes[8..16]);
    return c.PyBytes_FromStringAndSize(@ptrCast(&reordered), reordered.len);
}

fn uuidTimestamp(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    const self = uuidSelf(self_obj);
    return c.PyLong_FromUnsignedLongLong(self.hi >> state.UUID_TIMESTAMP_SHIFT);
}

fn uuidInt(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    const self = uuidSelf(self_obj);

    if (@hasDecl(c, "PyLong_FromUnsignedNativeBytes")) {
        var bytes: [16]u8 = undefined;

        uuid7.uuidPackBytes(self.hi, self.lo, &bytes);
        return c.PyLong_FromUnsignedNativeBytes(&bytes, 16, c.Py_ASNATIVEBYTES_BIG_ENDIAN | c.Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
    }

    const high = c.PyLong_FromUnsignedLongLong(self.hi);
    if (high == null) {
        return null;
    }
    defer pyDecRef(high);

    const bits = c.PyLong_FromLong(64);
    if (bits == null) {
        return null;
    }
    defer pyDecRef(bits);

    const shift = c.PyNumber_Lshift(high, bits);
    if (shift == null) {
        return null;
    }
    defer pyDecRef(shift);

    const low = c.PyLong_FromUnsignedLongLong(self.lo);
    if (low == null) {
        return null;
    }
    defer pyDecRef(low);

    return c.PyNumber_Or(shift, low);
}

fn uuidNbInt(self_obj: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    return uuidInt(self_obj, null);
}

fn uuidTimeLow(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLong(@truncate(uuidSelf(self_obj).hi >> 32));
}

fn uuidTimeMid(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLong(@truncate((uuidSelf(self_obj).hi >> 16) & 0xFFFF));
}

fn uuidTimeHiVersion(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLong(@truncate(uuidSelf(self_obj).hi & 0xFFFF));
}

fn uuidClockSeqHiVariant(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLong(@truncate(uuidSelf(self_obj).lo >> 56));
}

fn uuidClockSeqLow(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLong(@truncate((uuidSelf(self_obj).lo >> 48) & 0xFF));
}

fn uuidClockSeq(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    const self = uuidSelf(self_obj);
    const high: c_ulong = @truncate((self.lo >> 56) & 0x3F);
    const low: c_ulong = @truncate((self.lo >> 48) & 0xFF);
    return c.PyLong_FromUnsignedLong((high << 8) | low);
}

fn uuidNode(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    return c.PyLong_FromUnsignedLongLong(uuidSelf(self_obj).lo & 0xFFFF_FFFF_FFFF);
}

fn uuidFields(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    const self = uuidSelf(self_obj);
    return c.Py_BuildValue(
        "(kkkkkK)",
        @as(c_ulong, @truncate(self.hi >> 32)),
        @as(c_ulong, @truncate((self.hi >> 16) & 0xFFFF)),
        @as(c_ulong, @truncate(self.hi & 0xFFFF)),
        @as(c_ulong, @truncate(self.lo >> 56)),
        @as(c_ulong, @truncate((self.lo >> 48) & 0xFF)),
        @as(c_ulonglong, @truncate(self.lo & 0xFFFF_FFFF_FFFF)),
    );
}

fn uuidUrn(self_obj: ?*c.PyObject, _: ?*anyopaque) callconv(.c) ?*c.PyObject {
    const text = uuidStr(self_obj);
    defer if (text != null) pyDecRef(text);

    if (text == null) {
        return null;
    }

    return c.PyUnicode_FromFormat("urn:uuid:%U", text);
}

fn uuidCopy(self_obj: ?*c.PyObject, _: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    return pyIncRef(self_obj);
}

fn uuidDeepcopy(self_obj: ?*c.PyObject, args: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    var memo: ?*c.PyObject = null;

    if (c.PyArg_ParseTuple(args, "O:__deepcopy__", &memo) == 0) {
        return null;
    }

    return pyIncRef(self_obj);
}

fn uuidHash(self_obj: ?*c.PyObject) callconv(.c) c.Py_hash_t {
    const self = uuidSelf(self_obj);
    const mixed = self.hi ^ (self.hi >> 32) ^ self.lo ^ (self.lo >> 32);
    var hash: c.Py_hash_t = if (@bitSizeOf(c.Py_hash_t) == 64)
        @bitCast(mixed)
    else
        @bitCast(@as(u32, @truncate(mixed ^ (mixed >> 32))));

    if (hash == -1) {
        hash = -2;
    }

    return hash;
}

fn uuidCompare(left: *const UUIDObject, right: *const UUIDObject) c_int {
    if (left.hi != right.hi) {
        return if (left.hi < right.hi) -1 else 1;
    }
    if (left.lo != right.lo) {
        return if (left.lo < right.lo) -1 else 1;
    }
    return 0;
}

fn uuidRichcompare(a: ?*c.PyObject, b: ?*c.PyObject, op: c_int) callconv(.c) ?*c.PyObject {
    if (state.runtime.uuid_type == null or c.PyObject_TypeCheck(a, state.runtime.uuid_type) == 0 or c.PyObject_TypeCheck(b, state.runtime.uuid_type) == 0) {
        return pyIncRef(notImplementedObject());
    }

    const cmp = uuidCompare(@ptrCast(a.?), @ptrCast(b.?));

    if (op == c.Py_LT) {
        return c.PyBool_FromLong(@intFromBool(cmp < 0));
    }
    if (op == c.Py_LE) {
        return c.PyBool_FromLong(@intFromBool(cmp <= 0));
    }
    if (op == c.Py_EQ) {
        return c.PyBool_FromLong(@intFromBool(cmp == 0));
    }
    if (op == c.Py_NE) {
        return c.PyBool_FromLong(@intFromBool(cmp != 0));
    }
    if (op == c.Py_GT) {
        return c.PyBool_FromLong(@intFromBool(cmp > 0));
    }
    if (op == c.Py_GE) {
        return c.PyBool_FromLong(@intFromBool(cmp >= 0));
    }

    return pyIncRef(notImplementedObject());
}

fn pyUuid7(_: ?*c.PyObject, args: [*c]?*c.PyObject, nargs: c.Py_ssize_t, kwnames: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    const none_object = noneObject();
    var timestamp_obj: ?*c.PyObject = none_object;
    var nanos_obj: ?*c.PyObject = none_object;
    var mode_obj: ?*c.PyObject = none_object;
    const nkw: c.Py_ssize_t = if (kwnames != null) c.PyTuple_Size(kwnames) else 0;
    var hi: u64 = 0;
    var lo: u64 = 0;
    var mode: c_int = state.UUID_MODE_FAST;
    var i: c.Py_ssize_t = 0;

    if (nargs == 0 and nkw == 0) {
        if (uuid7.buildUuid7Default(&hi, &lo) != 0) {
            return null;
        }
        return @ptrCast(uuidNew(hi, lo));
    }

    if (nargs > 3) {
        c.PyErr_SetString(c.PyExc_TypeError, "uuid7() takes at most 3 positional arguments");
        return null;
    }

    if (nargs >= 1) timestamp_obj = args[0];
    if (nargs >= 2) nanos_obj = args[1];
    if (nargs >= 3) mode_obj = args[2];

    while (i < nkw) : (i += 1) {
        const key = c.PyTuple_GetItem(kwnames, i);
        const value = args[@intCast(nargs + i)];

        if (c.PyUnicode_CompareWithASCIIString(key, "timestamp") == 0) {
            timestamp_obj = value;
        } else if (c.PyUnicode_CompareWithASCIIString(key, "nanos") == 0) {
            nanos_obj = value;
        } else if (c.PyUnicode_CompareWithASCIIString(key, "mode") == 0) {
            mode_obj = value;
        } else {
            _ = c.PyErr_Format(c.PyExc_TypeError, "uuid7() got an unexpected keyword argument '%U'", key);
            return null;
        }
    }

    if (uuid7.parseMode(mode_obj, none_object, &mode) != 0) {
        return null;
    }

    if (mode == state.UUID_MODE_SECURE and timestamp_obj == none_object and nanos_obj == none_object) {
        if (uuid7.buildUuid7DefaultSecure(&hi, &lo) != 0) {
            return null;
        }
    } else if (uuid7.buildUuid7PartsFromArgs(timestamp_obj, nanos_obj, mode, none_object, &hi, &lo) != 0) {
        return null;
    }

    return @ptrCast(uuidNew(hi, lo));
}

fn pyReseedRng(_: ?*c.PyObject, _: ?*c.PyObject) callconv(.c) ?*c.PyObject {
    uuid7.reseedGeneratorState();
    return pyIncRef(noneObject());
}

var uuid_methods = [_]PyMethodDef{
    .{ .ml_name = "__copy__", .ml_meth = @ptrCast(@constCast(&uuidCopy)), .ml_flags = c.METH_NOARGS, .ml_doc = "Return self for copy.copy()." },
    .{ .ml_name = "__deepcopy__", .ml_meth = @ptrCast(@constCast(&uuidDeepcopy)), .ml_flags = c.METH_VARARGS, .ml_doc = "Return self for copy.deepcopy()." },
    .{},
};

var uuid_getset = [_]PyGetSetDef{
    .{ .name = "bytes", .get = @ptrCast(@constCast(&uuidBytes)), .doc = "UUID as 16 big-endian bytes." },
    .{ .name = "bytes_le", .get = @ptrCast(@constCast(&uuidBytesLe)), .doc = "UUID as 16 little-endian bytes." },
    .{ .name = "clock_seq", .get = @ptrCast(@constCast(&uuidClockSeq)), .doc = "Clock sequence." },
    .{ .name = "clock_seq_hi_variant", .get = @ptrCast(@constCast(&uuidClockSeqHiVariant)), .doc = "Clock sequence high byte with variant." },
    .{ .name = "clock_seq_low", .get = @ptrCast(@constCast(&uuidClockSeqLow)), .doc = "Clock sequence low byte." },
    .{ .name = "fields", .get = @ptrCast(@constCast(&uuidFields)), .doc = "UUID fields tuple." },
    .{ .name = "hex", .get = @ptrCast(@constCast(&uuidHex)), .doc = "Hexadecimal string." },
    .{ .name = "int", .get = @ptrCast(@constCast(&uuidInt)), .doc = "128-bit integer value." },
    .{ .name = "node", .get = @ptrCast(@constCast(&uuidNode)), .doc = "Node value." },
    .{ .name = "time", .get = @ptrCast(@constCast(&uuidTimestamp)), .doc = "UUID time value." },
    .{ .name = "time_hi_version", .get = @ptrCast(@constCast(&uuidTimeHiVersion)), .doc = "Time high field with version bits." },
    .{ .name = "time_low", .get = @ptrCast(@constCast(&uuidTimeLow)), .doc = "Time low field." },
    .{ .name = "time_mid", .get = @ptrCast(@constCast(&uuidTimeMid)), .doc = "Time middle field." },
    .{ .name = "timestamp", .get = @ptrCast(@constCast(&uuidTimestamp)), .doc = "Unix timestamp in milliseconds." },
    .{ .name = "urn", .get = @ptrCast(@constCast(&uuidUrn)), .doc = "UUID URN string." },
    .{},
};

var uuid_slots = [_]c.PyType_Slot{
    .{ .slot = c.Py_tp_methods, .pfunc = @ptrCast(@constCast(&uuid_methods)) },
    .{ .slot = c.Py_tp_getset, .pfunc = @ptrCast(@constCast(&uuid_getset)) },
    .{ .slot = c.Py_tp_repr, .pfunc = @ptrCast(@constCast(&uuidRepr)) },
    .{ .slot = c.Py_tp_str, .pfunc = @ptrCast(@constCast(&uuidStr)) },
    .{ .slot = c.Py_tp_hash, .pfunc = @ptrCast(@constCast(&uuidHash)) },
    .{ .slot = c.Py_tp_richcompare, .pfunc = @ptrCast(@constCast(&uuidRichcompare)) },
    .{ .slot = c.Py_nb_int, .pfunc = @ptrCast(@constCast(&uuidNbInt)) },
    .{ .slot = 0, .pfunc = null },
};

var uuid_spec = c.PyType_Spec{
    .name = "c_uuid_v7.UUID",
    .basicsize = @sizeOf(UUIDObject),
    .itemsize = 0,
    .flags = c.Py_TPFLAGS_DEFAULT,
    .slots = &uuid_slots,
};

var module_methods = [_]PyMethodDef{
    .{ .ml_name = "_uuid7", .ml_meth = @ptrCast(@constCast(&pyUuid7)), .ml_flags = c.METH_FASTCALL | c.METH_KEYWORDS, .ml_doc = "Generate a fast UUIDv7 object." },
    .{ .ml_name = "_reseed_rng", .ml_meth = @ptrCast(@constCast(&pyReseedRng)), .ml_flags = c.METH_NOARGS, .ml_doc = "Reseed the internal RNG state." },
    .{},
};

var zigmodule = PyModuleDef{
    .m_name = "_core",
    .m_doc = "Fast UUIDv7 generator.",
    .m_methods = &module_methods,
};

pub export fn PyInit__core() [*c]c.PyObject {
    const module = c.PyModule_Create(@as([*c]c.struct_PyModuleDef, @ptrCast(&zigmodule)));
    if (module == null) {
        return null;
    }

    const type_object = c.PyType_FromSpec(&uuid_spec);
    if (type_object == null) {
        pyDecRef(module);
        return null;
    }

    state.runtime.uuid_type = @ptrCast(type_object);

    if (c.PyModule_AddObject(module, "_UUID", type_object) < 0) {
        pyDecRef(type_object);
        pyDecRef(module);
        return null;
    }

    return module;
}
