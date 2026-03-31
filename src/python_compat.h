#ifndef COMPAT_H
#define COMPAT_H

#include <Python.h>

static Py_ssize_t c_uuid_v7_py_refcnt(PyObject *obj) {
    return Py_REFCNT(obj);
}

static PyModuleDef_Base c_uuid_v7_module_def_head_init() {
    return (PyModuleDef_Base)PyModuleDef_HEAD_INIT;
}

#endif
