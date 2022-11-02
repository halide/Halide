#include "Halide.h"
#include <iostream>

using namespace Halide;

class MyCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char *msg) override {
        std::cerr << "Should not see any warnings in this test, but saw: " << msg << "\n";
        exit(1);
    }

    void error(const char *msg) override {
        std::string m = msg;
        if (!strstr(msg, "Functions that are compute_at() a gpu_block() loop cannot have their own gpu_block() loops")) {
            std::cerr << "Did not see expected error, instead saw: (" << msg << ")\n";
            exit(1);
        }

        std::cout << "Success!\n";
        exit(0);
    }
};

int main(int argc, char **argv) {
    static MyCompileTimeErrorReporter reporter;
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
    a.compute_at(b, x).gpu_tile(x, xi, 4);

    b.realize({32, 32}, Target("host-metal"));

    std::cerr << "Failure, did not see error!\n";
    return 1;
}
