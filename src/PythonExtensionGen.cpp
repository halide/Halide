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

}  // namespace

PythonExtensionGen::PythonExtensionGen(std::ostream &dest)
    : dest(dest) {
}

void PythonExtensionGen::compile(const Module &module) {
    dest << "#include \"Python.h\"\n";
    dest << "#include \"HalideRuntime.h\"\n\n";

    // Emit extern decls of the Halide-generated functions we use directly
    // into this file, so that we don't have to #include the relevant .h
    // file directly; this simplifies certain compile/build setups (since
    // we don't have to build files in tandem and/or get include paths right),
    // and should be totally safe, since we are using the same codegen logic
    // that would be in the .h file anyway.
    {
        CodeGen_C extern_decl_gen(dest, module.target(), CodeGen_C::CPlusPlusExternDecl);
        extern_decl_gen.compile(module);
    }

    dest << "#define MODULE_NAME \"" << module.name() << "\"\n";

    dest << R"INLINE_CODE(
/* Older Python versions don't set up PyMODINIT_FUNC correctly. */
#if defined(_MSC_VER)
#define HALIDE_PYTHON_EXPORT      __declspec(dllexport)
#else
#define HALIDE_PYTHON_EXPORT      __attribute__((visibility("default")))
#endif

namespace {

template<int dimensions>
struct PyHalideBuffer {
    // Must allocate at least 1, even if d=0
    static constexpr int dims_to_allocate = (dimensions < 1) ? 1 : dimensions;

    Py_buffer py_buf;
    halide_dimension_t halide_dim[dims_to_allocate];
    halide_buffer_t halide_buf;
    bool py_buf_needs_release = false;
    bool halide_buf_valid = false;

    PyHalideBuffer(PyObject *py_obj, int flags, const char *name) {
        memset(&py_buf, 0, sizeof(py_buf));
        if (PyObject_GetBuffer(py_obj, &py_buf, PyBUF_FORMAT | PyBUF_STRIDED_RO | PyBUF_ANY_CONTIGUOUS | flags) < 0) {
            PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d", name, dimensions, py_buf.ndim);
            return;
        }
        py_buf_needs_release = true;

        if (dimensions && py_buf.ndim != dimensions) {
            PyErr_Format(PyExc_ValueError, "Invalid argument %s: Expected %d dimensions, got %d", name, dimensions, py_buf.ndim);
            return;
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
            return;
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
                return;
            }
        }
        if (halide_dim[py_buf.ndim - 1].extent * halide_dim[py_buf.ndim - 1].stride * py_buf.itemsize != py_buf.len) {
            PyErr_Format(PyExc_ValueError, "Invalid buffer: length %ld, but computed length %ld",
                         py_buf.len, py_buf.shape[0] * py_buf.strides[0]);
            return;
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
            if (*p == 'f' || *p == 'd') {
                // 'f' and 'd' are float and double, respectively.
                halide_buf.type.code = halide_type_float;
            } else if (*p >= 'a' && *p <= 'z') {
                // lowercase is signed int.
                halide_buf.type.code = halide_type_int;
            } else {
                // uppercase is unsigned int.
                halide_buf.type.code = halide_type_uint;
            }
            const char *type_codes = "bB?hHiIlLqQfd";  // integers and floats
            if (strchr(type_codes, *p)) {
                halide_buf.type.bits = (uint8_t)py_buf.itemsize * 8;
            } else {
                // We don't handle 's' and 'p' (char[]) and 'P' (void*)
                PyErr_Format(PyExc_ValueError, "Invalid data type for %s: %s", name, py_buf.format);
                return;
            }
        }
        halide_buf.type.lanes = 1;
        halide_buf.dimensions = py_buf.ndim;
        halide_buf.dim = halide_dim;
        halide_buf.host = (uint8_t *)py_buf.buf;
        halide_buf_valid = true;
    }

    ~PyHalideBuffer() {
        if (py_buf_needs_release) {
            PyBuffer_Release(&py_buf);
        }
    }

    PyHalideBuffer() = delete;
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
        }
    }

    dest << "\n";
    dest << "namespace {\n";
    dest << "\n";
    dest << "PyMethodDef _methods[] = {\n";
    for (const auto &f : module.functions()) {
        if (f.linkage == LinkageType::ExternalPlusMetadata) {
            const string basename = remove_namespaces(f.name);
            dest << "    {\"" << basename << "\", (PyCFunction)_f_" << basename
                 << ", METH_VARARGS|METH_KEYWORDS, nullptr},\n";
        }
    }
    dest << "    {0, 0, 0, nullptr},  // sentinel\n";
    dest << "};\n";

    dest << R"INLINE_CODE(
static_assert(PY_MAJOR_VERSION >= 3, "Python bindings for Halide require Python 3+");

struct PyModuleDef _moduledef = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    nullptr,
    -1,
    _methods,
};

}  // namespace

extern "C" {

HALIDE_PYTHON_EXPORT PyObject* PyInit_)INLINE_CODE";

    dest << module.name() << "(void) {";

    dest << R"INLINE_CODE(
    return PyModule_Create(&_moduledef);
}

}  // extern "C"

)INLINE_CODE";
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

    dest << "namespace {\n";
    dest << "\n";

    dest << indent << "const char* const _f_" << basename << "_kwlist[] = {\n";
    indent.indent += 2;
    for (size_t i = 0; i < args.size(); i++) {
        dest << indent << "\"" << arg_names[i] << "\",\n";
    }
    dest << indent << "nullptr\n";
    indent.indent -= 2;
    dest << indent << "};\n\n";

    dest << "// " << f.name << "\n";
    dest << "PyObject* _f_" << basename << "(PyObject* module, PyObject* args, PyObject* kwargs) {\n";

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
            dest << "}  // namespace\n";
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
    dest << "\", (char**)_f_" << basename << "_kwlist\n";
    for (size_t i = 0; i < args.size(); i++) {
        indent.indent += 2;
        dest << indent << ", &py_" << arg_names[i] << "\n";
        indent.indent -= 2;
    }
    dest << ")) {\n";
    indent.indent += 2;
    dest << indent << "PyErr_Format(PyExc_ValueError, \"Internal error\");\n";
    dest << indent << "return nullptr;\n";
    indent.indent -= 2;
    dest << indent << "}\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            const auto &name = arg_names[i];  // must use sanitized names here
            dest << indent << "PyHalideBuffer<" << (int)args[i].dimensions << "> b_" << name << "("
                 << "py_" << name << ", "
                 << (args[i].is_output() ? "PyBUF_WRITABLE" : "0") << ", "
                 << "_f_" << basename << "_kwlist[" << i << "]);\n";
            dest << indent << "if (!b_" << name << ".halide_buf_valid) {\n";
            indent.indent += 2;
            dest << indent << "return nullptr;\n";
            indent.indent -= 2;
            dest << indent << "}\n";
        }  // else Python already converted this.
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
    dest << indent << "PyErr_Format(PyExc_ValueError, \"Halide error %d\", result);\n";
    dest << indent << "return nullptr;\n";
    indent.indent -= 2;
    dest << indent << "}\n";
    dest << "\n";

    dest << indent << "Py_INCREF(Py_None);\n";
    dest << indent << "return Py_None;\n";
    indent.indent -= 2;
    dest << "}\n";
    dest << "\n";
    dest << "}  // namespace\n";
}

}  // namespace Internal
}  // namespace Halide
