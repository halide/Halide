#include <iostream>
#include <string>

#include "CodeGen_C.h"
#include "Module.h"
#include "PythonExtensionGen.h"
#include "Util.h"

// The buffer-protocol <-> halide_buffer_t marshalling helpers
// (Halide::PythonRuntime::unpack_buffer and PyHalideBuffer) live in a single
// source of truth, src/PythonExtensionRuntime.template.cpp, which is embedded
// here via binary2cpp and also compiled directly into the standalone
// `halide.runtime` Python module.
extern "C" unsigned char halide_c_template_PythonExtensionRuntime[];

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;

namespace {

// See normalize_line_endings in CodeGen_C.cpp for rationale.
string normalize_line_endings(const char *s) {
    string result;
    result.reserve(strlen(s));
    for (; *s; ++s) {
        if (*s != '\r') {
            result += *s;
        }
    }
    return result;
}

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

const string kModuleRegistrationCode = normalize_line_endings(R"INLINE_CODE(
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
)INLINE_CODE");

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

        // Emit the buffer marshalling helpers (Halide::PythonRuntime::unpack_buffer
        // and the PyHalideBuffer wrapper) from the shared source of truth. This is
        // the same implementation compiled into the standalone `halide.runtime`
        // module; see src/PythonExtensionRuntime.template.cpp.
        dest << "\n"
             << normalize_line_endings((const char *)halide_c_template_PythonExtensionRuntime)
             << "\n";

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
            dest << indent << "b_" << arg_names[i] << ".halide_buf->set_host_dirty();\n";
        }
    }
    dest << indent << "int result;\n";
    dest << indent << "Py_BEGIN_ALLOW_THREADS\n";
    dest << indent << "result = " << f.name << "(\n";
    indent.indent += 2;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            dest << indent << "b_" << arg_names[i] << ".halide_buf";
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
            dest << indent << "if (result == 0) result = halide_copy_to_host(nullptr, b_" << arg_names[i] << ".halide_buf);\n";
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
