#include "PyError.h"

namespace Halide {
namespace PythonBindings {

namespace {

void halide_python_error(void *, const char *msg) {
    throw Error(msg);
}

void halide_python_print(void *, const char *msg) {
    PySys_WriteStdout("%s", msg);
}

class HalidePythonCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char* msg) {
        PySys_WriteStdout("%s", msg);
    }

    void error(const char* msg) {
        throw Error(msg);
        // This method must not return!
    }
};

void translate_error(Error const &e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
}

}  // namespace

void define_error() {
    py::register_exception_translator<Error>(&translate_error);

    static HalidePythonCompileTimeErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    Halide::Internal::JITHandlers handlers;
    handlers.custom_error = halide_python_error;
    handlers.custom_print = halide_python_print;
    Halide::Internal::JITSharedRuntime::set_default_handlers(handlers);
}

}  // namespace PythonBindings
}  // namespace Halide

