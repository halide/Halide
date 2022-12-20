#include <iostream>
#include <string>

#include "CodeGen_C.h"
#include "Module.h"
#include "PythonExtensionGen.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;

namespace {

string sanitize_name(const string &name) {
    ostringstream oss;
    for (char c : name) {
        if (c == '.' || c == '_') {
            oss << "_";
        } else if (!isalnum(c)) {
            oss << "_" << (int)c;
        } else {
            oss << c;
        }
    }
    return oss.str();
}

string remove_namespaces(const string &name) {
    size_t i = name.find_last_of(':');
    if (i == string::npos) {
        return name;
    } else {
        return name.substr(i + 1);
    }
}

bool can_convert(const LoweredArgument *arg) {
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
    if (arg->type.is_float() && arg->type.bits() != 32 && arg->type.bits() != 64 && arg->type.bits() != 16) {
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

std::pair<string, string> print_type(const LoweredArgument *arg) {
    // Excluded by can_convert() above:
    internal_assert(!arg->type.is_vector());

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
        // } else if (arg->type.is_float() && arg->type.bits() == 16) {
        //     TODO: can't pass scalar float16 type
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

const char kModuleRegistrationCode[] = R"INLINE_CODE(
static_assert(PY_MAJOR_VERSION >= 3, "Python bindings for Halide require Python 3+");

namespace Halide::PythonExtensions {
#define X(name) extern PyObject *name(PyObject *module, PyObject *args, PyObject *kwargs);
      HALIDE_PYTHON_EXTENSION_FUNCTIONS
#undef X
}  // namespace Halide::PythonExtensions

namespace {

#define _HALIDE_STRINGIFY(x)            #x
#define _HALIDE_EXPAND_AND_STRINGIFY(x) _HALIDE_STRINGIFY(x)
#define _HALIDE_CONCAT(x, y)            x##y
#define _HALIDE_EXPAND_AND_CONCAT(x, y) _HALIDE_CONCAT(x, y)

PyMethodDef _methods[] = {
  #define X(name) {#name, reinterpret_cast<PyCFunction>(Halide::PythonExtensions::name), METH_VARARGS | METH_KEYWORDS, nullptr},
  HALIDE_PYTHON_EXTENSION_FUNCTIONS
  #undef X
  {0, 0, 0, nullptr},  // sentinel
};

PyModuleDef _moduledef = {
    PyModuleDef_HEAD_INIT,                                              // base
    _HALIDE_EXPAND_AND_STRINGIFY(HALIDE_PYTHON_EXTENSION_MODULE_NAME),  // name
    nullptr,                                                            // doc
    -1,                                                                 // size
    _methods,                                                           // methods
    nullptr,                                                            // slots
    nullptr,                                                            // traverse
    nullptr,                                                            // clear
    nullptr,                                                            // free
};

#ifndef HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS
void _module_halide_error(void *user_context, const char *msg) {
    // Most Python code probably doesn't want to log the error text to stderr,
    // so we won't do that by default.
    #ifdef HALIDE_PYTHON_EXTENSION_LOG_ERRORS_TO_STDERR
    PyGILState_STATE s = PyGILState_Ensure();
    PySys_FormatStderr("%s\n", msg);
    PyGILState_Release(s);
    #endif
}

void _module_halide_print(void *user_context, const char *msg) {
    PyGILState_STATE s = PyGILState_Ensure();
    PySys_FormatStdout("%s", msg);
    PyGILState_Release(s);
}
#endif  // HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS

}  // namespace

namespace Halide::PythonRuntime {

bool unpack_buffer(PyObject *py_obj,
                   int py_getbuffer_flags,
                   const char *name,
                   int dimensions,
                   Py_buffer &py_buf,
                   halide_dimension_t *halide_dim,
                   halide_buffer_t &halide_buf,
                   bool &py_buf_valid) {
    py_buf_valid = false;

    memset(&py_buf, 0, sizeof(py_buf));
    if (PyObject_GetBuffer(py_obj, &py_buf, PyBUF_FORMAT | PyBUF_STRIDED_RO | PyBUF_ANY_CONTIGUOUS | py_getbuffer_flags) < 0) {
        PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d", name, dimensions, py_buf.ndim);
        return false;
    }
    py_buf_valid = true;

    if (dimensions && py_buf.ndim != dimensions) {
        PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d", name, dimensions, py_buf.ndim);
        return false;
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
    if (PyBuffer_IsContiguous(&py_buf, 'F')) {
        j = 0;
        j_step = 1;
    } else if (PyBuffer_IsContiguous(&py_buf, 'C')) {
        j = py_buf.ndim - 1;
        j_step = -1;
    } else {
        /* Python checks all dimensions and strides, so this typically indicates
         * a bug in the array's buffer protocol. */
        PyErr_Format(PyExc_ValueError, "Invalid buffer: neither C nor Fortran contiguous");
        return false;
    }
    for (i = 0; i < py_buf.ndim; ++i, j += j_step) {
        halide_dim[i].min = 0;
        halide_dim[i].stride = (int)(py_buf.strides[j] / py_buf.itemsize);  // strides is in bytes
        halide_dim[i].extent = (int)py_buf.shape[j];
        halide_dim[i].flags = 0;
        if (py_buf.suboffsets && py_buf.suboffsets[i] >= 0) {
            // Halide doesn't support arrays of pointers. But we should never see this
            // anyway, since we specified PyBUF_STRIDED.
            PyErr_Format(PyExc_ValueError, "Invalid buffer: suboffsets not supported");
            return false;
        }
    }
    if (halide_dim[py_buf.ndim - 1].extent * halide_dim[py_buf.ndim - 1].stride * py_buf.itemsize != py_buf.len) {
        PyErr_Format(PyExc_ValueError, "Invalid buffer: length %ld, but computed length %ld",
                     py_buf.len, py_buf.shape[0] * py_buf.strides[0]);
        return false;
    }

    memset(&halide_buf, 0, sizeof(halide_buf));
    if (!py_buf.format) {
        halide_buf.type.code = halide_type_uint;
        halide_buf.type.bits = 8;
    } else {
        /* Convert struct type code. See
         * https://docs.python.org/2/library/struct.html#module-struct */
        char *p = py_buf.format;
        while (strchr("@<>!=", *p)) {
            p++;  // ignore little/bit endian (and alignment)
        }
        if (*p == 'f' || *p == 'd' || *p == 'e') {
            // 'f', 'd', and 'e' are float, double, and half, respectively.
            halide_buf.type.code = halide_type_float;
        } else if (*p >= 'a' && *p <= 'z') {
            // lowercase is signed int.
            halide_buf.type.code = halide_type_int;
        } else {
            // uppercase is unsigned int.
            halide_buf.type.code = halide_type_uint;
        }
        const char *type_codes = "bBhHiIlLqQfde";  // integers and floats
        if (*p == '?') {
            // Special-case bool, so that it is a distinct type vs uint8_t
            // (even though the memory layout is identical)
            halide_buf.type.bits = 1;
        } else if (strchr(type_codes, *p)) {
            halide_buf.type.bits = (uint8_t)py_buf.itemsize * 8;
        } else {
            // We don't handle 's' and 'p' (char[]) and 'P' (void*)
            PyErr_Format(PyExc_ValueError, "Invalid data type for %s: %s", name, py_buf.format);
            return false;
        }
    }
    halide_buf.type.lanes = 1;
    halide_buf.dimensions = py_buf.ndim;
    halide_buf.dim = halide_dim;
    halide_buf.host = (uint8_t *)py_buf.buf;

    return true;
}

}  // namespace Halide::PythonRuntime

extern "C" {

HALIDE_EXPORT_SYMBOL PyObject *_HALIDE_EXPAND_AND_CONCAT(PyInit_, HALIDE_PYTHON_EXTENSION_MODULE_NAME)() {
    PyObject *m = PyModule_Create(&_moduledef);
    #ifndef HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS
    halide_set_error_handler(_module_halide_error);
    halide_set_custom_print(_module_halide_print);
    #endif  // HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS
    return m;
}

}  // extern "C"
)INLINE_CODE";

}  // namespace

PythonExtensionGen::PythonExtensionGen(std::ostream &dest)
    : dest(dest) {
}

void PythonExtensionGen::compile(const Module &module) {
    dest << "#include <string>\n";
    dest << "#include <Python.h>\n";
    dest << "#include \"HalideRuntime.h\"\n\n";

    std::vector<std::string> fnames;

    // Emit extern decls of the Halide-generated functions we use directly
    // into this file, so that we don't have to #include the relevant .h
    // file directly; this simplifies certain compile/build setups (since
    // we don't have to build files in tandem and/or get include paths right),
    // and should be totally safe, since we are using the same codegen logic
    // that would be in the .h file anyway.
    if (!module.functions().empty()) {
        // The CodeGen_C dtor must run to finish codegen correctly,
        // so wrap this in braces
        {
            CodeGen_C extern_decl_gen(dest, module.target(), CodeGen_C::CPlusPlusExternDecl);
            extern_decl_gen.compile(module);
        }

        dest << R"INLINE_CODE(
namespace Halide::PythonRuntime {
extern bool unpack_buffer(PyObject *py_obj,
                          int py_getbuffer_flags,
                          const char *name,
                          int dimensions,
                          Py_buffer &py_buf,
                          halide_dimension_t *halide_dim,
                          halide_buffer_t &halide_buf,
                          bool &py_buf_valid);
}  // namespace Halide::PythonRuntime

namespace {

template<int dimensions>
struct PyHalideBuffer {
    // Must allocate at least 1, even if d=0
    static constexpr int dims_to_allocate = (dimensions < 1) ? 1 : dimensions;

    Py_buffer py_buf;
    halide_dimension_t halide_dim[dims_to_allocate];
    halide_buffer_t halide_buf;
    bool py_buf_needs_release = false;

    bool unpack(PyObject *py_obj, int py_getbuffer_flags, const char *name) {
        return Halide::PythonRuntime::unpack_buffer(py_obj, py_getbuffer_flags, name, dimensions, py_buf, halide_dim, halide_buf, py_buf_needs_release);
    }

    ~PyHalideBuffer() {
        if (py_buf_needs_release) {
            PyBuffer_Release(&py_buf);
        }
    }

    PyHalideBuffer() = default;
    PyHalideBuffer(const PyHalideBuffer &other) = delete;
    PyHalideBuffer &operator=(const PyHalideBuffer &other) = delete;
    PyHalideBuffer(PyHalideBuffer &&other) = delete;
    PyHalideBuffer &operator=(PyHalideBuffer &&other) = delete;
};

}  // namespace

)INLINE_CODE";

        for (const auto &f : module.functions()) {
            if (f.linkage == LinkageType::ExternalPlusMetadata) {
                compile(f);
                fnames.push_back(remove_namespaces(f.name));
            }
        }
    }

    dest << "\n";
    if (!fnames.empty()) {
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_OMIT_MODULE_DEFINITION\n";
        dest << "\n";
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_MODULE_NAME\n";
        dest << "#define HALIDE_PYTHON_EXTENSION_MODULE_NAME " << module.name() << "\n";
        dest << "#endif  // HALIDE_PYTHON_EXTENSION_MODULE_NAME\n";
        dest << "\n";
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_FUNCTIONS\n";
        dest << "#define HALIDE_PYTHON_EXTENSION_FUNCTIONS";
        for (const auto &fname : fnames) {
            dest << " X(" << fname << ")";
        }
        dest << "\n";
        dest << "#endif  // HALIDE_PYTHON_EXTENSION_FUNCTIONS\n";
        dest << "\n";
    }
    dest << kModuleRegistrationCode;

    if (!fnames.empty()) {
        dest << "#endif  // HALIDE_PYTHON_EXTENSION_OMIT_MODULE_DEFINITION\n";
    }
}

void PythonExtensionGen::compile(const LoweredFunc &f) {
    const std::vector<LoweredArgument> &args = f.args;
    const string basename = remove_namespaces(f.name);

    std::vector<string> arg_names(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_names[i] = sanitize_name(args[i].name);
    }

    Indentation indent;
    indent.indent = 0;

    dest << "namespace Halide::PythonExtensions {\n";
    dest << "\n";
    dest << "namespace {\n";
    dest << "\n";
    dest << indent << "const char* const " << basename << "_kwlist[] = {\n";
    indent.indent += 2;
    for (size_t i = 0; i < args.size(); i++) {
        dest << indent << "\"" << arg_names[i] << "\",\n";
    }
    dest << indent << "nullptr\n";
    indent.indent -= 2;
    dest << indent << "};\n";
    dest << "\n";
    dest << "}  // namespace\n";
    dest << "\n";
    dest << "// " << f.name << "\n";
    dest << "PyObject *" << basename << "(PyObject *module, PyObject *args, PyObject *kwargs) {\n";

    indent.indent += 2;

    for (const auto &arg : args) {
        if (!can_convert(&arg)) {
            /* Some arguments can't be converted to Python yet. In those
             * cases, just add a dummy function that always throws an
             * Exception. */
            // TODO: Add support for handles and vectors.
            // TODO: might make more sense to simply fail at Halide compile time!
            dest << indent << "PyErr_Format(PyExc_NotImplementedError, "
                 << "\"Can't convert argument " << arg.name << " from Python\");\n";
            dest << indent << "return nullptr;\n";
            dest << "}\n";
            dest << "}  // namespace Halide::PythonExtensions\n";
            return;
        }
    }

    for (size_t i = 0; i < args.size(); i++) {
        dest << indent << print_type(&args[i]).second << " py_" << arg_names[i] << ";\n";
    }
    dest << indent << "if (!PyArg_ParseTupleAndKeywords(args, kwargs, \"";
    for (const auto &arg : args) {
        dest << print_type(&arg).first;
    }
    dest << "\", (char**)" << basename << "_kwlist\n";
    indent.indent += 2;
    for (size_t i = 0; i < args.size(); i++) {
        dest << indent << ", &py_" << arg_names[i] << "\n";
    }
    indent.indent -= 2;
    dest << indent << ")) {\n";
    indent.indent += 2;
    dest << indent << "PyErr_Format(PyExc_ValueError, \"Internal error\");\n";
    dest << indent << "return nullptr;\n";
    indent.indent -= 2;
    dest << indent << "}\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            const auto &name = arg_names[i];  // must use sanitized names here
            dest << indent << "PyHalideBuffer<" << (int)args[i].dimensions << "> b_" << name << ";\n";
        }
    }
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            const auto &name = arg_names[i];  // must use sanitized names here
            dest << indent << "if (!b_" << name << ".unpack(py_" << name << ", "
                 << (args[i].is_output() ? "PyBUF_WRITABLE" : "0") << ", "
                 << basename << "_kwlist[" << i << "])) return nullptr;\n";
        }
    }
    dest << "\n";
    // Mark all input buffers as having a dirty host, so that the Halide call will
    // do a lazy-copy-to-GPU if needed.
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer() && args[i].is_input()) {
            dest << indent << "b_" << arg_names[i] << ".halide_buf.set_host_dirty();\n";
        }
    }
    dest << indent << "int result;\n";
    dest << indent << "Py_BEGIN_ALLOW_THREADS\n";
    dest << indent << "result = " << f.name << "(\n";
    indent.indent += 2;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            dest << indent << "&b_" << arg_names[i] << ".halide_buf";
        } else {
            dest << indent << "py_" << arg_names[i] << "";
        }
        if (i < args.size() - 1) {
            dest << ",";
        }
        dest << "\n";
    }
    indent.indent -= 2;
    dest << indent << ");\n";
    dest << indent << "Py_END_ALLOW_THREADS\n";
    // Since the Python Buffer protocol is host-memory-only, we *must*
    // flush results back to host, otherwise the output buffer will contain
    // random garbage. (We need a better solution for this, see https://github.com/halide/Halide/issues/6868)
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer() && args[i].is_output()) {
            dest << indent << "if (result == 0) result = halide_copy_to_host(nullptr, &b_" << arg_names[i] << ".halide_buf);\n";
        }
    }
    dest << indent << "if (result != 0) {\n";
    indent.indent += 2;
    dest << indent << "#ifndef HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS\n";
    dest << indent << "PyErr_Format(PyExc_RuntimeError, \"Halide Runtime Error: %d\", result);\n";
    dest << indent << "#else\n";
    dest << indent << "PyErr_Format(PyExc_ValueError, \"Halide error %d\", result);\n";
    dest << indent << "#endif  // HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS\n";
    dest << indent << "return nullptr;\n";
    indent.indent -= 2;
    dest << indent << "}\n";
    dest << "\n";

    dest << indent << "Py_INCREF(Py_None);\n";
    dest << indent << "return Py_None;\n";
    indent.indent -= 2;
    dest << "}\n";
    dest << "\n";
    dest << "}  // namespace Halide::PythonExtensions\n";
}

}  // namespace Internal
}  // namespace Halide
