#include "PyError.h"

namespace Halide {
namespace PythonBindings {

namespace {

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
        // This method is called *only* from the Compiler -- never from jitted
        // code -- so throwing an Error here is the right thing to do.

        throw Error(msg);

        // This method must not return!
    }
};

}  // namespace

PyJITUserContext::PyJITUserContext()
    : JITUserContext() {
    handlers.custom_print = halide_python_print;
    // No: we don't want a custom error function.
    // If we leave it as the default, realize() and infer_input_bounds()
    // will correctly propagate the final error message to halide_runtime_error,
    // which will throw an exception at the end of the relevant call.
    //
    // (It's tempting to override custom_error to just do 'throw Error',
    // but when called from jitted code, it likely won't be able to find
    // an enclosing C++ try block, meaning it could call std::terminate.)
    //
    // handlers.custom_error = halide_python_error;
}

void define_error(py::module &m) {
    static HalidePythonCompileTimeErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

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
