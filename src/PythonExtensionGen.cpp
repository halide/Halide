#include <string>
#include <vector>

#include "CodeGen_C.h"
#include "Module.h"
#include "PythonExtensionGen.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {

string remove_namespaces(const string &name) {
    const size_t i = name.find_last_of(':');
    return i == string::npos ? name : name.substr(i + 1);
}

const char kRuntimeAPI[] = R"INLINE_CODE(
struct HalidePythonRuntimeAPI {
    unsigned api_version;
    unsigned halide_version_major;
    PyObject *(*make_kernel)(const char *name,
                             int (*argv_fn)(void **),
                             const halide_filter_metadata_t *metadata,
                             halide_error_handler_t (*set_error_handler)(halide_error_handler_t),
                             halide_print_t (*set_custom_print)(halide_print_t),
                             int expose_user_context,
                             int log_errors_to_stderr);
};
)INLINE_CODE";

const char kModuleRegistrationCode[] = R"INLINE_CODE(
#ifndef HALIDE_PYTHON_EXTENSION_FUNCTIONS
#define HALIDE_PYTHON_EXTENSION_FUNCTIONS
#endif

namespace Halide::PythonExtensions {
#define X(name) PyObject *make_##name(const HalidePythonRuntimeAPI *api);
HALIDE_PYTHON_EXTENSION_FUNCTIONS
#undef X
}  // namespace Halide::PythonExtensions

namespace {

#define _HALIDE_STRINGIFY(x)            #x
#define _HALIDE_EXPAND_AND_STRINGIFY(x) _HALIDE_STRINGIFY(x)
#define _HALIDE_CONCAT(x, y)            x##y
#define _HALIDE_EXPAND_AND_CONCAT(x, y) _HALIDE_CONCAT(x, y)

PyModuleDef _moduledef = {
    PyModuleDef_HEAD_INIT,
    _HALIDE_EXPAND_AND_STRINGIFY(HALIDE_PYTHON_EXTENSION_MODULE_NAME),
    nullptr,
    -1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

extern "C" {

HALIDE_EXPORT_SYMBOL PyObject *_HALIDE_EXPAND_AND_CONCAT(PyInit_, HALIDE_PYTHON_EXTENSION_MODULE_NAME)() {
    constexpr unsigned expected_api_version = 1;
    PyObject *runtime_module = PyImport_ImportModule("halide.runtime._runtime");
    if (!runtime_module) {
        return nullptr;
    }
    PyObject *capsule = PyObject_GetAttrString(runtime_module, "_C_API");
    Py_DECREF(runtime_module);
    if (!capsule) {
        return nullptr;
    }
    auto *api = static_cast<const HalidePythonRuntimeAPI *>(
        PyCapsule_GetPointer(capsule, "halide.runtime._runtime._C_API"));
    Py_DECREF(capsule);
    if (!api) {
        return nullptr;
    }
    if (api->api_version != expected_api_version ||
        api->halide_version_major != HALIDE_VERSION_MAJOR) {
        PyErr_Format(
            PyExc_ImportError,
            "Incompatible halide-runtime C API: got API %u / Halide %u, expected API %u / Halide %u",
            api->api_version,
            api->halide_version_major,
            expected_api_version,
            HALIDE_VERSION_MAJOR);
        return nullptr;
    }

    PyObject *module = PyModule_Create(&_moduledef);
    if (!module) {
        return nullptr;
    }

#define X(name)                                                               \
    do {                                                                      \
        PyObject *kernel = Halide::PythonExtensions::make_##name(api);         \
        if (!kernel) {                                                        \
            Py_DECREF(module);                                                \
            return nullptr;                                                   \
        }                                                                     \
        if (PyModule_AddObject(module, #name, kernel) < 0) {                  \
            Py_DECREF(kernel);                                                \
            Py_DECREF(module);                                                \
            return nullptr;                                                   \
        }                                                                     \
    } while (false);
    HALIDE_PYTHON_EXTENSION_FUNCTIONS
#undef X

    return module;
}

}  // extern "C"
)INLINE_CODE";

}  // namespace

PythonExtensionGen::PythonExtensionGen(std::ostream &dest)
    : dest(dest) {
}

void PythonExtensionGen::compile(const Module &module) {
    dest << "#include <Python.h>\n";
    dest << "#include \"HalideRuntime.h\"\n\n";
    dest << kRuntimeAPI << "\n";

    std::vector<std::string> fnames;

    if (!module.functions().empty()) {
        // Emit declarations for the linked AOT functions, including their
        // _argv and _metadata entry points, without requiring a generated header.
        {
            CodeGen_C extern_decl_gen(dest, module.target(), CodeGen_C::CPlusPlusExternDecl);
            extern_decl_gen.compile(module);
        }

        for (const auto &f : module.functions()) {
            if (f.linkage != LinkageType::ExternalPlusMetadata) {
                continue;
            }

            const string basename = remove_namespaces(f.name);
            fnames.push_back(basename);

            dest << "\nnamespace Halide::PythonExtensions {\n\n";
            dest << "PyObject *make_" << basename
                 << "(const HalidePythonRuntimeAPI *api) {\n";
            dest << "    return api->make_kernel(\n";
            dest << "        \"" << basename << "\",\n";
            dest << "        " << f.name << "_argv,\n";
            dest << "        " << f.name << "_metadata(),\n";
            dest << "#ifdef HALIDE_PYTHON_EXTENSION_OMIT_ERROR_AND_PRINT_HANDLERS\n";
            dest << "        nullptr, nullptr,\n";
            dest << "#else\n";
            dest << "        &halide_set_error_handler, &halide_set_custom_print,\n";
            dest << "#endif\n";
            dest << "        1,\n";
            dest << "#ifdef HALIDE_PYTHON_EXTENSION_LOG_ERRORS_TO_STDERR\n";
            dest << "        1);\n";
            dest << "#else\n";
            dest << "        0);\n";
            dest << "#endif\n";
            dest << "}\n\n";
            dest << "}  // namespace Halide::PythonExtensions\n";
        }
    }

    dest << "\n";
    if (!fnames.empty()) {
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_OMIT_MODULE_DEFINITION\n\n";
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_MODULE_NAME\n";
        dest << "#define HALIDE_PYTHON_EXTENSION_MODULE_NAME " << module.name() << "\n";
        dest << "#endif\n\n";
        dest << "#ifndef HALIDE_PYTHON_EXTENSION_FUNCTIONS\n";
        dest << "#define HALIDE_PYTHON_EXTENSION_FUNCTIONS";
        for (const auto &fname : fnames) {
            dest << " X(" << fname << ")";
        }
        dest << "\n#endif\n\n";
    }
    dest << kModuleRegistrationCode;

    if (!fnames.empty()) {
        dest << "#endif  // HALIDE_PYTHON_EXTENSION_OMIT_MODULE_DEFINITION\n";
    }
}

}  // namespace Internal
}  // namespace Halide
