// selected and merged from the various translated C version generated using:
// zig translate-c -I /opt/python/<VER>/include/python3.*/ sum.c

const c = @cImport({
    @cDefine("PY_SSIZE_T_CLEAN", {});
    @cInclude("Python.h");
});

const PyObject = c.PyObject;

const PyModuleDef_Base = extern struct {
    ob_base: PyObject,
    m_init: ?*const fn () callconv(.c) [*c]PyObject = null,
    m_index: c.Py_ssize_t = 0,
    m_copy: [*c]PyObject = null,
};

const PyModuleDef_HEAD_INIT = if ((c.PY_MAJOR_VERSION > 2) and (c.PY_MINOR_VERSION > 12) and @hasDecl(c, "Py_GIL_DISABLED"))
    if ((c.PY_MAJOR_VERSION > 2) and (c.PY_MINOR_VERSION > 13))
        PyModuleDef_Base {
            .ob_base = PyObject {
                .ob_tid = 0,
                .ob_flags = 4,
                .ob_mutex = c.PyMutex{
                    ._bits = 0,
                },
                .ob_gc_bits = 0,
                .ob_ref_local = @as(c_uint, 4294967295),
                .ob_ref_shared = 0,
                .ob_type = null,
            }
        }
    else
        PyModuleDef_Base {
            .ob_base = PyObject {
                .ob_tid = 0,
                .ob_mutex = c.PyMutex{
                    ._bits = 0,
                },
                .ob_gc_bits = 0,
                .ob_ref_local = @as(c_uint, 4294967295),
                .ob_ref_shared = 0,
                .ob_type = null,
            }
        }
else if ((c.PY_MAJOR_VERSION > 2) and (c.PY_MINOR_VERSION > 13))
        PyModuleDef_Base {
            .ob_base = PyObject {
                .unnamed_0 = .{ .ob_refcnt_full = 1 },
                .ob_type = null,
            }
        }
    else if ((c.PY_MAJOR_VERSION > 2) and (c.PY_MINOR_VERSION > 11))
            PyModuleDef_Base {
                .ob_base = PyObject {
                    .unnamed_0 = .{ .ob_refcnt = 1 },
                    .ob_type = null,
                }
            }
        else
            PyModuleDef_Base {
                .ob_base = PyObject {
                    .ob_refcnt = 1,
                    .ob_type = null,
                }
            };

const PyMethodDef = extern struct {
    ml_name: [*c]const u8 = null,
    ml_meth: c.PyCFunction = null,
    ml_flags: c_int = 0,
    ml_doc: [*c]const u8 = null,
};

const PyModuleDef = extern struct {
    // m_base: c.PyModuleDef_Base,
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

/////////////////////////////////////////////////

fn pyLongToCLong(obj: [*c]PyObject) ?c_long {
    const value = c.PyLong_AsLong(obj);
    if (value == -1 and c.PyErr_Occurred() != null) {
        return null;
    }
    return value;
}

fn pyExcTypeError() [*c]PyObject {
    const exc = c.PyExc_TypeError;
    return switch (@typeInfo(@TypeOf(exc))) {
        .pointer => |ptr| switch (@typeInfo(ptr.child)) {
            .@"fn" => exc(),
            else => @constCast(exc),
        },
        .@"fn" => exc(),
        else => @compileError("unsupported PyExc_TypeError representation"),
    };
}

pub export fn sum(self: [*]PyObject, args: [*]PyObject) [*c]PyObject {
    _ = self;

    const argc = c.PyTuple_Size(args);
    if (argc != 2) {
        c.PyErr_SetString(pyExcTypeError(), "sum() takes exactly 2 positional arguments");
        return null;
    }

    const a_obj = c.PyTuple_GetItem(args, 0);
    if (a_obj == null) {
        return null;
    }

    const b_obj = c.PyTuple_GetItem(args, 1);
    if (b_obj == null) {
        return null;
    }

    const a = pyLongToCLong(a_obj) orelse return null;
    const b = pyLongToCLong(b_obj) orelse return null;

    return c.PyLong_FromLong(a + b);
}

pub var methods = [_]PyMethodDef{
    PyMethodDef{
        .ml_name = "sum",
        .ml_meth = @ptrCast(&sum),
        .ml_flags = @as(c_int, 1),
        .ml_doc = null,
    },
    PyMethodDef{
        .ml_name = null,
        .ml_meth = null,
        .ml_flags = 0,
        .ml_doc = null,
    },
};

pub var zigmodule = PyModuleDef{
    .m_name = "_core",
    .m_methods = &methods,
};

pub export fn PyInit__core() [*c]c.PyObject {
    const m: [*c]PyObject = c.PyModule_Create(@as([*c]c.struct_PyModuleDef, @ptrCast(&zigmodule)));
    if (m == null)
        return null;
    if ((c.PY_MAJOR_VERSION > 2) and (c.PY_MINOR_VERSION > 12) and @hasDecl(c, "Py_GIL_DISABLED")) {
        _ = c.PyUnstable_Module_SetGIL(m, c.Py_MOD_GIL_NOT_USED);
    }
    return m;
}
