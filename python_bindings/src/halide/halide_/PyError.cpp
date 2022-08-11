#include "PyError.h"

namespace Halide {
namespace PythonBindings {

namespace {

void halide_python_error(JITUserContext *, const char *msg) {
    throw Error(msg);
}

void halide_python_print(JITUserContext *, const char *msg) {
    py::gil_scoped_acquire acquire;
    py::print(msg, py::arg("end") = "");
}

class HalidePythonCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char *msg) override {
        halide_python_print(nullptr, msg);
    }

    void error(const char *msg) override {
        throw Error(msg);
        // This method must not return!
    }
};

}  // namespace

void define_error(py::module &m) {
    static HalidePythonCompileTimeErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    Halide::JITHandlers handlers;
    handlers.custom_error = halide_python_error;
    handlers.custom_print = halide_python_print;
    Halide::Internal::JITSharedRuntime::set_default_handlers(handlers);

    static py::exception<Error> halide_error(m, "HalideError");
    py::register_exception_translator([](std::exception_ptr p) {  // NOLINT
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const Error &e) {
            halide_error(e.what());
        }
    });
}

}  // namespace PythonBindings
}  // namespace Halide
