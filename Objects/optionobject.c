/* Option object implementation */

#include "Python.h"
#include "pycore_object.h"      /* _Py_SetImmortal etc., version-dependent */
#include "optionobject.h"

/* ---- State ---------------------------------------------------------- */

/* Represent state with a PyObject*:
 *   value == NULL  -> Nothing
 *   value != NULL  -> Some(value)  (may be Py_None, but never NULL)
 */
typedef struct {
    PyObject_HEAD
    PyObject *value;   /* NULL when Nothing */
} PyOptionObject;

#define PyOption_CAST(op) ((PyOptionObject *)(op))
#define PyOption_IS_NOTHING(op) (PyOption_CAST(op)->value == NULL)

/* ---- Singleton ------------------------------------------------------ */

static PyOptionObject nothing_singleton;

/* ---- Constructors (C API) ------------------------------------------ */

PyObject *
PyOption_Some(PyObject *value)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Some() requires a value");
        return NULL;
    }
    PyOptionObject *op = PyObject_New(PyOptionObject, &PyOption_Type);
    if (op == NULL) {
        return NULL;
    }
    Py_INCREF(value);
    op->value = value;
    return (PyObject *)op;
}

PyObject *
PyOption_Nothing(void)
{
    return Py_NewRef((PyObject *)&nothing_singleton);
}

/* ---- tp_new / tp_init ---------------------------------------------- */

/* Option(value) is equivalent to Some(value); Option() is equivalent
 * to Nothing. This makes Option itself the constructor, so a separate
 * "Some" name is not strictly required.
 */
static PyObject *
option_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"value", NULL};
    PyObject *value = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:Option", kwlist, &value)) {
        return NULL;
    }

    /* Use type rather than &PyOption_Type to allow subclassing (like list) */
    PyOptionObject *op = (PyOptionObject *)type->tp_alloc(type, 0);
    if (op == NULL) {
        return NULL;
    }
    if (value == NULL) {
        op->value = NULL;          /* Nothing */
    }
    else {
        Py_INCREF(value);
        op->value = value;         /* Some(value) */
    }
    return (PyObject *)op;
}

/* ---- tp_dealloc ----------------------------------------------------- */

static void
option_dealloc(PyOptionObject *op)
{
    /* The singleton is never deallocated; early-return as a safeguard */
    if (op == &nothing_singleton) {
        return;
    }
    Py_XDECREF(op->value);
    Py_TYPE(op)->tp_free((PyObject *)op);
}

/* ---- tp_repr -------------------------------------------------------- */

static PyObject *
option_repr(PyOptionObject *op)
{
    if (PyOption_IS_NOTHING(op)) {
        return PyUnicode_FromString("Nothing");
    }
    PyObject *val_repr = PyObject_Repr(op->value);
    if (val_repr == NULL) {
        return NULL;
    }
    PyObject *result = PyUnicode_FromFormat("Some(%U)", val_repr);
    Py_DECREF(val_repr);
    return result;
}

/* ---- Methods -------------------------------------------------------- */

static PyObject *
option_is_some(PyOptionObject *op, PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(!PyOption_IS_NOTHING(op));
}

static PyObject *
option_is_none(PyOptionObject *op, PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(PyOption_IS_NOTHING(op));
}

static PyObject *
option_unwrap(PyOptionObject *op, PyObject *Py_UNUSED(ignored))
{
    if (PyOption_IS_NOTHING(op)) {
        PyErr_SetString(PyExc_ValueError, "called `unwrap()` on Nothing");
        return NULL;
    }
    return Py_NewRef(op->value);
}

static PyObject *
option_expect(PyOptionObject *op, PyObject *arg)
{
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "expect() argument must be str");
        return NULL;
    }
    if (PyOption_IS_NOTHING(op)) {
        PyErr_SetObject(PyExc_ValueError, arg);
        return NULL;
    }
    return Py_NewRef(op->value);
}

static PyObject *
option_unwrap_or(PyOptionObject *op, PyObject *default_value)
{
    if (PyOption_IS_NOTHING(op)) {
        return Py_NewRef(default_value);
    }
    return Py_NewRef(op->value);
}

static PyObject *
option_unwrap_or_else(PyOptionObject *op, PyObject *func)
{
    if (!PyOption_IS_NOTHING(op)) {
        return Py_NewRef(op->value);
    }
    return PyObject_CallNoArgs(func);
}

static PyObject *
option_map(PyOptionObject *op, PyObject *func)
{
    if (PyOption_IS_NOTHING(op)) {
        return PyOption_Nothing();
    }
    PyObject *mapped = PyObject_CallOneArg(func, op->value);
    if (mapped == NULL) {
        return NULL;
    }
    PyObject *result = PyOption_Some(mapped);
    Py_DECREF(mapped);
    return result;
}

static PyObject *
option_map_or(PyOptionObject *op, PyObject *args)
{
    PyObject *default_value, *func;
    if (!PyArg_ParseTuple(args, "OO:map_or", &default_value, &func)) {
        return NULL;
    }
    if (PyOption_IS_NOTHING(op)) {
        return Py_NewRef(default_value);
    }
    return PyObject_CallOneArg(func, op->value);
}

static PyObject *
option_and_then(PyOptionObject *op, PyObject *func)
{
    if (PyOption_IS_NOTHING(op)) {
        return PyOption_Nothing();
    }
    /* The callback must return an Option */
    return PyObject_CallOneArg(func, op->value);
}

static PyObject *
option_or_else(PyOptionObject *op, PyObject *func)
{
    if (!PyOption_IS_NOTHING(op)) {
        return Py_NewRef((PyObject *)op);
    }
    return PyObject_CallNoArgs(func);
}

static PyObject *
option_filter(PyOptionObject *op, PyObject *pred)
{
    if (PyOption_IS_NOTHING(op)) {
        return PyOption_Nothing();
    }
    PyObject *keep = PyObject_CallOneArg(pred, op->value);
    if (keep == NULL) {
        return NULL;
    }
    int truthy = PyObject_IsTrue(keep);
    Py_DECREF(keep);
    if (truthy < 0) {
        return NULL;
    }
    return truthy ? Py_NewRef((PyObject *)op) : PyOption_Nothing();
}

/* ok_or / ok_or_else need the Result type; use a deferred import to
 * avoid a circular dependency at startup.
 */
static PyObject *
option_ok_or(PyOptionObject *op, PyObject *err)
{
    /* Result is a builtin; call builtins.Ok / builtins.Err directly. */
    PyObject *builtins = PyImport_ImportModule("builtins");
    if (builtins == NULL) {
        return NULL;
    }
    PyObject *r;
    if (PyOption_IS_NOTHING(op)) {
        r = PyObject_CallMethod(builtins, "Err", "O", err);
    }
    else {
        r = PyObject_CallMethod(builtins, "Ok", "O", op->value);
    }
    Py_DECREF(builtins);
    return r;
}

static PyMethodDef option_methods[] = {
    {"is_some",       (PyCFunction)option_is_some,        METH_NOARGS,  NULL},
    {"is_none",       (PyCFunction)option_is_none,        METH_NOARGS,  NULL},
    {"unwrap",        (PyCFunction)option_unwrap,         METH_NOARGS,  NULL},
    {"expect",        (PyCFunction)option_expect,         METH_O,       NULL},
    {"unwrap_or",     (PyCFunction)option_unwrap_or,      METH_O,       NULL},
    {"unwrap_or_else",(PyCFunction)option_unwrap_or_else, METH_O,       NULL},
    {"map",           (PyCFunction)option_map,            METH_O,       NULL},
    {"map_or",        (PyCFunction)option_map_or,         METH_VARARGS, NULL},
    {"and_then",      (PyCFunction)option_and_then,       METH_O,       NULL},
    {"or_else",       (PyCFunction)option_or_else,        METH_O,       NULL},
    {"filter",        (PyCFunction)option_filter,         METH_O,       NULL},
    {"ok_or",         (PyCFunction)option_ok_or,          METH_O,       NULL},
    {NULL, NULL, 0, NULL}
};

/* ---- Type object ---------------------------------------------------- */

PyTypeObject PyOption_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "Option",
    .tp_basicsize = sizeof(PyOptionObject),
    .tp_dealloc   = (destructor)option_dealloc,
    .tp_repr      = (reprfunc)option_repr,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc       = "Option type, representing Some(value) or Nothing.",
    .tp_methods   = option_methods,
    .tp_new       = option_new,
};

/* ---- Module-level init (call from your init path) ------------------- */

int
_PyOption_InitTypes(void)
{
    if (PyType_Ready(&PyOption_Type) < 0) {
        return -1;
    }

    /* Initialize the Nothing singleton */
    PyObject_INIT((PyObject *)&nothing_singleton, &PyOption_Type);
    nothing_singleton.value = NULL;

    /* Keep the singleton alive forever (immortal in 3.12+, pinned before) */
#if PY_VERSION_HEX >= 0x030C0000
    _Py_SetImmortal((PyObject *)&nothing_singleton);
#else
    Py_INCREF((PyObject *)&nothing_singleton);  /* permanent +1 */
#endif
    return 0;
}

/* ---- Builtin "Some" wrapper ---------------------------------------- */

PyObject *
PyOption_BuiltinSome(PyObject *module, PyObject *value)
{
    return PyOption_Some(value);
}
