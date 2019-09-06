#include <iostream>
#include <string>

#include "Module.h"
#include "PythonExtensionGen.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;

static string sanitize_name(const string &name) {
    ostringstream oss;
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.' || name[i] == '_') {
            oss << '_';
        } else if (!isalnum(name[i])) {
            oss << "_" << (int)name[i];
        } else {
          oss << name[i];
        }
    }
    return oss.str();
}

static const string remove_namespaces(const string &name) {
    size_t i = name.find_last_of(":");
    if (i == string::npos) {
        return name;
    } else {
        return name.substr(i + 1);
    }
}

static bool has_legacy_buffers(const LoweredFunc& func) {
    const std::vector<LoweredArgument> &args = func.args;
    auto legacy_buffer_type = type_of<buffer_t *>().handle_type;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].type.is_handle() && args[i].type.handle_type == legacy_buffer_type) {
            return true;
        }
    }
    return false;
}

static bool can_convert(const LoweredArgument* arg) {
  if (arg->type.is_handle()) {
      if (arg->name == "__user_context") {
          /* __user_context is a void* pointer to a user supplied memory region.
           * We allow the Python callee to pass PyObject* pointers to that. */
          return true;
      } else {
          return false;
      }
  }
  if (arg->type.is_vector()) {
      return false;
  }
  if (arg->type.is_float() && arg->type.bits() != 32 && arg->type.bits() != 64) {
      return false;
  }
  if (arg->is_buffer() && arg->type.bits() == 1) {
      // The Python buffer API doesn't support bit arrays.
      return false;
  }
  if ((arg->type.is_int() || arg->type.is_uint()) &&
      arg->type.bits() != 1 &&
      arg->type.bits() != 8 && arg->type.bits() != 16 &&
      arg->type.bits() != 32 && arg->type.bits() != 64) {
      return false;
  }
  return true;
}

std::pair<string, string> print_type(const LoweredArgument* arg) {
    // Excluded by can_convert() above:
    assert(!arg->type.is_vector());

    if (arg->type.is_handle()) {
        /* Handles can be any pointer. However, from Python, all you can pass to
         * a function is a PyObject*, so we can restrict to that. */
        return std::make_pair("O", "PyObject*");
    } else if (arg->is_buffer()) {
        return std::make_pair("O", "PyObject*");
    } else if (arg->type.is_float() && arg->type.bits() == 32) {
        return std::make_pair("f", "float");
    } else if (arg->type.is_float() && arg->type.bits() == 64) {
        return std::make_pair("d", "double");
    } else if (arg->type.bits() == 1) {
        // "b" expects an unsigned char, so we assume that bool == uint8.
        return std::make_pair("b", "bool");
    } else if (arg->type.is_int() && arg->type.bits() == 64) {
        return std::make_pair("L", "long long");
    } else if (arg->type.is_uint() && arg->type.bits() == 64) {
        return std::make_pair("K", "unsigned long long");
    } else if (arg->type.is_int()) {
        return std::make_pair("i", "int");
    } else if (arg->type.is_uint()) {
        return std::make_pair("I", "unsigned int");
    } else {
        return std::make_pair("E", "unknown type");
    }
}

void PythonExtensionGen::convert_buffer(string name, const LoweredArgument* arg) {
    assert(arg->is_buffer());
    assert(arg->dimensions);
    dest << "    halide_buffer_t buffer_" << name << ";\n";
    dest << "    halide_dimension_t dimensions_" << name << "[" << (int)arg->dimensions << "];\n";
    dest << "    if (_convert_py_buffer_to_halide(";
    dest << /*pyobj*/ "py_" << name << ", ";
    dest << /*dimensions*/ (int)arg->dimensions << ", ";
    dest << /*flags*/ (arg->is_output() ? "PyBUF_WRITABLE" : "0") << ", ";
    dest << /*dim*/ "dimensions_" << name << ", ";
    dest << /*out*/ "&buffer_" << name << ", ";
    dest << /*name*/ "\"" << name << "\"";
    dest << ") < 0) {\n";
    dest << "        return NULL;\n";
    dest << "    }\n";
}

PythonExtensionGen::PythonExtensionGen(std::ostream &dest, const std::string &header_name, Target target)
    : dest(dest), header_name(header_name), target(target) {
}

void PythonExtensionGen::compile(const Module &module) {
    dest << "#include \"" << header_name << "\"\n";
    dest << "#include \"Python.h\"\n";
    dest << "#include \"HalideRuntime.h\"\n\n";

    dest << "#define MODULE_NAME \"" << module.name() << "\"\n";

    dest << R"INLINE_CODE(
/* Older Python versions don't set up PyMODINIT_FUNC correctly. */
#if defined(WIN32) || defined(_WIN32)
#    define HALIDE_PYTHON_EXPORT __declspec(dllexport)
#else
#    define HALIDE_PYTHON_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

static __attribute__((unused)) int _convert_py_buffer_to_halide(
        PyObject* pyobj, int dimensions, int flags,
        halide_dimension_t* dim,  // array of size `dimensions`
        halide_buffer_t* out, const char* name) {
    Py_buffer buf;
    int ret = PyObject_GetBuffer(
      pyobj, &buf, PyBUF_FORMAT | PyBUF_STRIDED_RO | PyBUF_ANY_CONTIGUOUS | flags);
    if (ret < 0) {
      return ret;
    }
    if (dimensions && buf.ndim != dimensions) {
      PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d",
                   name, dimensions, buf.ndim);
      return -1;
    }
    /* We'll get a buffer that's either:
     * C_CONTIGUOUS (last dimension varies the fastest, i.e., has stride=1) or
     * F_CONTIGUOUS (first dimension varies the fastest, i.e., has stride=1).
     * The latter is preferred, since it's already in the format that Halide
     * needs. It can can be achieved in numpy by passing order='F' during array
     * creation. However, if we do get a C_CONTIGUOUS buffer, flip the dimensions
     * (transpose) so we can process it without having to reallocate.
     */
    int i, j, j_step;
    if (PyBuffer_IsContiguous(&buf, 'F')) {
      j = 0;
      j_step = 1;
    } else if (PyBuffer_IsContiguous(&buf, 'C')) {
      j = buf.ndim - 1;
      j_step = -1;
    } else {
      /* Python checks all dimensions and strides, so this typically indicates
       * a bug in the array's buffer protocol. */
      PyErr_Format(PyExc_ValueError, "Invalid buffer: neither C nor Fortran contiguous");
      return -1;
    }
    for (i = 0; i < buf.ndim; ++i, j += j_step) {
        dim[i].min = 0;
        dim[i].stride = (int)(buf.strides[j] / buf.itemsize); // strides is in bytes
        dim[i].extent = (int)buf.shape[j];
        dim[i].flags = 0;
        if (buf.suboffsets && buf.suboffsets[i] >= 0) {
            // Halide doesn't support arrays of pointers. But we should never see this
            // anyway, since we specified PyBUF_STRIDED.
            PyErr_Format(PyExc_ValueError, "Invalid buffer: suboffsets not supported");
            return -1;
        }
    }
    if (dim[buf.ndim - 1].extent * dim[buf.ndim - 1].stride * buf.itemsize != buf.len) {
        PyErr_Format(PyExc_ValueError, "Invalid buffer: length %ld, but computed length %ld",
                     buf.len, buf.shape[0] * buf.strides[0]);
        return -1;
    }
    *out = halide_buffer_t();
    if (!buf.format) {
        out->type.code = halide_type_uint;
        out->type.bits = 8;
    } else {
        /* Convert struct type code. See
         * https://docs.python.org/2/library/struct.html#module-struct */
        char* p = buf.format;
        while (strchr("@<>!=", *p)) {
            p++;  // ignore little/bit endian (and alignment)
        }
        if (*p == 'f' || *p == 'd') {
            // 'f' and 'd' are float and double, respectively.
            out->type.code = halide_type_float;
        } else if (*p >= 'a' && *p <= 'z') {
            // lowercase is signed int.
            out->type.code = halide_type_int;
        } else {
            // uppercase is unsigned int.
            out->type.code = halide_type_uint;
        }
        const char* type_codes = "bB?hHiIlLqQfd";  // integers and floats
        if (strchr(type_codes, *p)) {
            out->type.bits = buf.itemsize * 8;
        } else {
            // We don't handle 's' and 'p' (char[]) and 'P' (void*)
            PyErr_Format(PyExc_ValueError, "Invalid data type for %s: %s", name, buf.format);
            return -1;
        }
    }
    out->type.lanes = 1;
    out->dimensions = buf.ndim;
    out->dim = dim;
    out->host = (uint8_t*)buf.buf;
    return 0;
}

)INLINE_CODE";

    for (auto &f : module.functions()) {
        if (!has_legacy_buffers(f)) {
            compile(f);
        }
    }

    dest << "\n";
    dest << "static PyMethodDef _methods[] = {\n";
    for (auto &f : module.functions()) {
        /* With the legacy_buffer_wrappers feature, Halide stores every function
         * twice, once with new and once with old buffers. Ignore the latter. */
        if (!has_legacy_buffers(f)) {
            const string basename = remove_namespaces(f.name);
            dest << "    {\"" << basename << "\", (PyCFunction)_f_" << basename
                 << ", METH_VARARGS|METH_KEYWORDS, NULL},\n";
        }
    }
    dest << "    {0, 0, 0, NULL},  // sentinel\n";
    dest << "};\n";

    dest << R"INLINE_CODE(
#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef _moduledef = {
    PyModuleDef_HEAD_INIT,
    .m_name=MODULE_NAME,
    .m_doc=NULL,
    .m_size=-1,
    .m_methods=_methods,
};
HALIDE_PYTHON_EXPORT PyObject* PyInit_)INLINE_CODE";
    dest << module.name() << "(void) {";
    dest << R"INLINE_CODE(
    return PyModule_Create(&_moduledef);
}
#else
HALIDE_PYTHON_EXPORT void init)INLINE_CODE";
    dest << module.name() << "(void) {";
    dest << R"INLINE_CODE(
    Py_InitModule3(MODULE_NAME, _methods, NULL);
}
#endif

#ifdef __cplusplus
}
#endif
)INLINE_CODE";
}

void PythonExtensionGen::compile(const LoweredFunc &f) {
    const std::vector<LoweredArgument> &args = f.args;
    const string basename = remove_namespaces(f.name);
    std::vector<string> arg_names(args.size());
    dest << "// " << f.name << "\n";
    dest << "static PyObject* _f_" << basename << "(PyObject* module, PyObject* args, PyObject* kwargs) {\n";
    for (size_t i = 0; i < args.size(); i++) {
        arg_names[i] = sanitize_name(args[i].name);
        if (!can_convert(&args[i])) {
            /* Some arguments can't be converted to Python yet. In those
             * cases, just add a dummy function that always throws an
             * Exception. */
            // TODO: Add support for handles and vectors.
            dest << "    PyErr_Format(PyExc_NotImplementedError, "
                 << "\"Can't convert argument " << args[i].name << " from Python\");\n";
            dest << "    return NULL;\n";
            dest << "}";
            return;
        }
    }
    dest << "    static const char* kwlist[] = {";
    for (size_t i = 0; i < args.size(); i++) {
        dest << "\"" << arg_names[i] << "\", ";
    }
    dest << "NULL};\n";
    for (size_t i = 0; i < args.size(); i++) {
        dest << "    " << print_type(&args[i]).second << " py_" << arg_names[i] << ";\n";
    }
    dest << "    if (!PyArg_ParseTupleAndKeywords(args, kwargs, \"";
    for (size_t i = 0; i < args.size(); i++) {
        dest << print_type(&args[i]).first;
    }
    dest << "\", (char**)kwlist";
    for (size_t i = 0; i < args.size(); i++) {
        dest << ", ";
        dest << "&py_" << arg_names[i];
    }
    dest << ")) {\n";
    dest << "        return NULL;\n";
    dest << "    }\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            convert_buffer(arg_names[i], &args[i]);
        } else {
            // Python already converted this.
        }
    }
    dest << "    int result = " << f.name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) {
            dest << ", ";
        }
        if (args[i].is_buffer()) {
            dest << "&buffer_" << arg_names[i];
        } else {
            dest << "py_" << arg_names[i];
        }
    }
    dest << ");";
    dest << R"INLINE_CODE(
    if (result != 0) {
        /* In the optimal case, we'd be generating an exception declared
         * in python_bindings/src, but since we're self-contained,
         * we don't have access to that API. */
        PyErr_Format(PyExc_ValueError, "Halide error %d", result);
        return NULL;
    }
    Py_INCREF(Py_True);
    return Py_True;
)INLINE_CODE";
    dest << "}\n";
}

}
}
