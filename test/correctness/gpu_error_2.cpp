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
        if (!strstr(msg, "Functions that are compute_at() a gpu_block() loop must specify the innermost gpu_block() loop for that Func.")) {
            std::cerr << "Did not see expected error, instead saw: (" << msg << ")\n";
            exit(1);
        }

        std::cout << "Saw expected error message.\n";
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
    a(x, y) += 1;
    b(x, y) = a(x, y);

    Var xi, yi;
    b.gpu_tile(x, y, xi, yi, 4, 4);
    a.compute_at(b, y);

    b.realize({32, 32}, Target("host-metal"));

    std::cerr << "Failure, did not see error!\n";
    return 1;
}
