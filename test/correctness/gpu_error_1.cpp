#include "Halide.h"
#include <iostream>

using namespace Halide;

class HalidePythonCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char *msg) override {
        std::cerr << "Should not see any warnings in this test, but saw: " << msg << "\n";
        exit(1);
    }

    void error(const char *msg) override {
        std::string m = msg;
        if (m != "Error: Invalid schedule: Loop over a.s0.x.x.__block_id_x cannot be inside of a different Func's gpu_blocks() loop, but was inside b.s0.y.y.__block_id_y\n") {
            std::cerr << "Did not see expected error, instead saw: (" << m << ")\n";
            exit(1);
        }

        std::cout << "Success!\n";
        exit(0);
    }
};

int main(int argc, char **argv) {
    static HalidePythonCompileTimeErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    ImageParam im(Float(32), 2);

    Func a("a"), b("b");
    Var x("x"), y("y");

    a(x, y) = im(x, y);
    b(x, y) = a(x, y);

    // Verify that attempting to schedule such that we would have nested gpu-blocks for different
    // functions produces a useful error message.
    Var xi, yi;
    b.gpu_tile(x, y, xi, yi, 4, 4);
    a.compute_at(b, y).gpu_tile(x, xi, 4);

    b.realize({32, 32}, Target("host-metal"));

    std::cerr << "Failure, did not see error!\n";
    return 1;
}
