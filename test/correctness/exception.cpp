#include "Halide.h"
#include <iostream>

using namespace Halide;

void check_pure(Func f) {
    if (f.has_update_definition()) {
        std::cout << "f's reduction definition was supposed to fail!\n";
        exit(-1);
    }
}

void check_error(bool error) {
    if (!error) {
        std::cout << "There was supposed to be an error!\n";
        exit(-1);
    }
}

int main(int argc, char **argv) {

    if (!Halide::exceptions_enabled()) {
        std::cout << "[SKIP] Halide was compiled without exceptions.\n";
        return 0;
    }

    // Try some invalid Function definitions
    Func f1;
    Var x;
    bool error;

    try {
        error = false;
        f1(x) = x + 3;

        // Bad because first arg is a float
        f1(x / 3.0f) += 1;
    } catch (const Halide::CompileError &e) {
        error = true;
        std::cout << "Expected compile error:\n"
                  << e.what() << "\n";
    }
    // We should have entered the catch block
    check_error(error);

    // At this point, f should have only its pure definition
    check_pure(f1);

    try {
        error = false;
        // Bad because f is not a Tuple
        f1(x) += f1(x)[1];
    } catch (const Halide::CompileError &e) {
        error = true;
        std::cout << "Expected compile error:\n"
                  << e.what() << "\n";
    }
    check_error(error);
    check_pure(f1);

    try {
        error = false;
        // Bad because the RHS is the wrong type;
        f1(x) += 1.3f;
    } catch (const Halide::CompileError &e) {
        error = true;
        std::cout << "Expected compile error:\n"
                  << e.what() << "\n";
    }
    check_error(error);
    check_pure(f1);

    try {
        error = false;
        Expr e;
        RDom r(0, 10);
        // Bad because e is undefined
        f1(r) = e;
    } catch (const Halide::CompileError &e) {
        error = true;
        std::cout << "Expected compile error:\n"
                  << e.what() << "\n";
    }
    check_error(error);
    check_pure(f1);

    // Now do some things that count as internal errors
    try {
        error = false;
        Expr a, b;
        Internal::Add::make(a, b);
    } catch (const Halide::InternalError &e) {
        error = true;
        std::cout << "Expected internal error:\n"
                  << e.what() << "\n";
    }
    check_error(error);

    try {
        error = false;
        Internal::modulus_remainder(x > 3.0f);
    } catch (const Halide::InternalError &e) {
        error = true;
        std::cout << "Expected internal error:\n"
                  << e.what() << "\n";
    }
    check_error(error);

    // Now some runtime errors
    ImageParam im(Float(32), 1);
    Func f2;
    f2(x) = im(x) * 2.0f;
    try {
        error = false;
        f2.realize({10});
    } catch (const Halide::RuntimeError &e) {
        error = true;
        std::cout << "Expected runtime error:\n"
                  << e.what() << "\n";
    }
    check_error(error);
    // Oops, forgot to bind im. Lets try again:
    Buffer<float> an_image(10);
    lambda(x, x * 7.0f).realize(an_image);
    im.set(an_image);
    Buffer<float> result = f2.realize({10});
    for (size_t i = 0; i < 10; i++) {
        float correct = i * 14.0f;
        if (result(i) != correct) {
            std::cout << "result(" << i
                      << ") = " << result(i)
                      << " instead of " << correct << "\n";
            return -1;
        }
    }

    try {
        error = false;
        Param<int> p;
        p.set_range(0, 10);
        p.set(-4);
        Func f4;
        f4(x) = p;
        f4.realize({10});
    } catch (const Halide::RuntimeError &e) {
        error = true;
        std::cout << "Expected runtime error:\n"
                  << e.what() << "\n";
    }
    check_error(error);

    std::cout << "Success!\n";
    return 0;
}
