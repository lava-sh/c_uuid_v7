const builtin = @import("builtin");

const RomuTrio = @import("romu_trio.zig");

const c = @import("c.zig").c;

pub const UUIDObject = extern struct {
    ob_base: c.PyObject,
    hi: u64,
    lo: u64,
};

pub const UUID7Args = struct {
    timestamp_ms: u64 = 0,
    nanos: u64 = 0,
    has_timestamp: bool = false,
    has_nanos: bool = false,
};

pub const UUID_TIMESTAMP_SHIFT: u6 = 16;
pub const UUID_VERSION_BITS: u64 = 0x7000;
pub const UUID_VARIANT_BITS: u64 = 0x8000_0000_0000_0000;
pub const UUID_MAX_TIMESTAMP_MS: u64 = 0xFFFF_FFFF_FFFF;
pub const UUID_MAX_TIMESTAMP_S: u64 = UUID_MAX_TIMESTAMP_MS / 1000;
pub const UUID_MAX_NANOS: u64 = 1_000_000_000;
pub const UUID_V7_MAX_COUNTER: u64 = (1 << 42) - 1;
pub const UUID_MODE_FAST: c_int = 0;
pub const UUID_MODE_SECURE: c_int = 1;

pub const UUID_RAND_MASK: u64 = 0x3FFF_FFFF_FFFF_FFFF;
pub const RESEED_MASK: u64 = (1 << 41) - 1;
pub const LOW30_MASK: u64 = (1 << 30) - 1;

pub const RuntimeState = struct {
    uuid_type: ?*c.PyTypeObject = null,
    reusable_uuid: ?*UUIDObject = null,
    none_object: ?*c.PyObject = null,
    not_implemented_object: ?*c.PyObject = null,
    type_error: ?*c.PyObject = null,
    value_error: ?*c.PyObject = null,
    os_error: ?*c.PyObject = null,
    last_timestamp_ms: u64 = 0,
    counter42: u64 = 0,
    prng: RomuTrio = undefined,
    prng_seeded: bool = false,
    epoch_base_ms: u64 = 0,
    tick_base_ms: u64 = 0,
};

pub var runtime = RuntimeState{};

fn loadBuiltinAttr(name: [*:0]const u8) ?*c.PyObject {
    const builtins = c.PyImport_ImportModule("builtins") orelse return null;
    defer c.Py_DecRef(builtins);
    return c.PyObject_GetAttrString(builtins, name);
}

pub fn initPythonObjects() bool {
    if (builtin.os.tag != .windows) {
        return true;
    }

    runtime.none_object = loadBuiltinAttr("None") orelse return false;
    errdefer clearPythonObjects();
    runtime.not_implemented_object = loadBuiltinAttr("NotImplemented") orelse return false;
    runtime.type_error = loadBuiltinAttr("TypeError") orelse return false;
    runtime.value_error = loadBuiltinAttr("ValueError") orelse return false;
    runtime.os_error = loadBuiltinAttr("OSError") orelse return false;
    return true;
}

pub fn clearPythonObjects() void {
    inline for (&.{
        &runtime.none_object,
        &runtime.not_implemented_object,
        &runtime.type_error,
        &runtime.value_error,
        &runtime.os_error,
    }) |value| {
        if (value.*) |obj| {
            c.Py_DecRef(obj);
            value.* = null;
        }
    }
}

pub fn pyNone() ?*c.PyObject {
    if (builtin.os.tag == .windows) {
        return runtime.none_object;
    }
    return @constCast(c.Py_None());
}

pub fn pyNotImplemented() ?*c.PyObject {
    if (builtin.os.tag == .windows) {
        return runtime.not_implemented_object;
    }
    return @constCast(c.Py_NotImplemented());
}

pub fn pyExcTypeError() ?*c.PyObject {
    if (builtin.os.tag == .windows) {
        return runtime.type_error;
    }
    return c.PyExc_TypeError;
}

pub fn pyExcValueError() ?*c.PyObject {
    if (builtin.os.tag == .windows) {
        return runtime.value_error;
    }
    return c.PyExc_ValueError;
}

pub fn pyExcOSError() ?*c.PyObject {
    if (builtin.os.tag == .windows) {
        return runtime.os_error;
    }
    return c.PyExc_OSError;
}
