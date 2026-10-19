#ifndef Py_OPTIONOBJECT_H
#define Py_OPTIONOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyOption_Type;

#define PyOption_Check(op) Py_IS_TYPE((op), &PyOption_Type)

PyAPI_FUNC(PyObject *) PyOption_Some(PyObject *value);
PyAPI_FUNC(PyObject *) PyOption_Nothing(void);
PyAPI_FUNC(int)        _PyOption_InitTypes(void);
PyAPI_FUNC(PyObject *) PyOption_BuiltinSome(PyObject *module, PyObject *value);

#ifdef __cplusplus
}
#endif
#endif /* !Py_OPTIONOBJECT_H */
