// selected and merged from the various translated C version generated using:
// zig translate-c -I /opt/python/<VER>/include/python3.*/ sum.c

const std = @import("std");
const c = @cImport({
    @cDefine("PY_SSIZE_T_CLEAN", {});
    @cInclude("Python.h");
});

const PyObject = c.PyObject;

/////////////////////////////////////////////////

fn pyLongToCLong(obj: [*c]PyObject) ?c_long {
    const value = c.PyLong_AsLong(obj);
    if (value == -1 and c.PyErr_Occurred() != null) {
        return null;
    }
    return value;
}

fn execModule(module: [*c]PyObject) c_int {
    _ = module;
    return 0;
}

pub export fn sum(self: [*]PyObject, args: [*]PyObject) [*c]PyObject {
    _ = self;

    var a_obj: [*c]PyObject = undefined;
    var b_obj: [*c]PyObject = undefined;
    if (c.PyArg_UnpackTuple(args, "sum", 2, 2, &a_obj, &b_obj) == 0) {
        return null;
    }

    const a = pyLongToCLong(a_obj) orelse return null;
    const b = pyLongToCLong(b_obj) orelse return null;

    return c.PyLong_FromLong(a + b);
}

pub var methods = [_]c.PyMethodDef{
    c.PyMethodDef{
        .ml_name = "sum",
        .ml_meth = @ptrCast(&sum),
        .ml_flags = @as(c_int, 1),
        .ml_doc = null,
    },
    c.PyMethodDef{
        .ml_name = null,
        .ml_meth = null,
        .ml_flags = 0,
        .ml_doc = null,
    },
};

pub var module_slots = [_]c.struct_PyModuleDef_Slot{
    .{
        .slot = c.Py_mod_exec,
        .value = @constCast(@ptrCast(&execModule)),
    },
    .{
        .slot = 0,
        .value = null,
    },
};

pub var zigmodule = c.struct_PyModuleDef{
    .m_base = std.mem.zeroes(c.PyModuleDef_Base),
    .m_name = "_core",
    .m_doc = null,
    .m_size = 0,
    .m_methods = &methods,
    .m_slots = &module_slots,
    .m_traverse = null,
    .m_clear = null,
    .m_free = null,
};

pub export fn PyInit__core() [*c]c.PyObject {
    return c.PyModuleDef_Init(@as([*c]c.struct_PyModuleDef, @ptrCast(&zigmodule)));
}
