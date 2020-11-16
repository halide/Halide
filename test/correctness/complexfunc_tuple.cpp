#include "../../apps/fft/complex.h"
#include "Halide.h"
#include <cmath>
#include <complex>
#include <iostream>

using namespace Halide;
using std::cout;
using std::endl;

#define N 5

void print_buf(const char *prefix, std::complex<double> *buf, int X, int Y) {
    for (int y = 0; y < Y; y++) {
        printf("%s row %d = [", prefix, y);
        for (int x = 0; x < X; x++) {
            std::complex<double> value = buf[y * X + x];
            printf("%c%4.1f+%.1fi ", x ? ',' : ' ', value.real(), value.imag());
        }
        printf("]\n");
    }
}

Buffer<double> gen_buf() {
    Buffer<double> input(2, 5);
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();
    for (int i = 0; i < N; i++) {
        std::complex<double> value(1.0 + i, i - 1.0);
        input_ptr[i] = value;
    }
    return input;
}

void test_io() {
    printf("test_io\n");
    // test that complex values can be passed into and out of a Halide kernel.
    Buffer<double> input = gen_buf();

    Var c("c"), x("x");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x) = input_complex(x);
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x) = select(c == 0, result(x).re(), result(x).im());

    Buffer<double> output = interleaved_output.realize(2, N);

    for (int i = 0; i < N; i++) {
        std::complex<double> value(1.0 + i, i - 1.0);
        if (std::abs(output(0, i) - value.real()) > 0.01) {
            std::cout << "Wrong real value for element " << i << ". Expected " << value.real() << ", got " << output(0, i) << endl;
            abort();
        }
        if (std::abs(output(1, i) - value.imag()) > 0.01) {
            std::cout << "Wrong imaginary value for element " << i << ". Expected " << value.imag() << ", got " << output(1, i) << endl;
            abort();
        }
    }
}

void test_ops_complex_complex() {
    printf("test_ops_complex_complex\n");
    Buffer<double> input = gen_buf();
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = ComplexExpr(Expr(0.0), Expr(0.0));
    result(x, 0) = ComplexExpr(Expr(1.1), Expr(2.2)) + input_complex(x);
    result(x, 1) = ComplexExpr(Expr(3.3), Expr(4.4)) - input_complex(x);
    result(x, 2) = ComplexExpr(Expr(5.5), Expr(6.6)) * input_complex(x);
    //result(x, 3) = ComplexExpr(Expr(7.7), Expr(8.8)) / input_complex(x);
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());

    Buffer<double> output = interleaved_output.realize(2, N, 4);

    Buffer<double> expected(2, N, 4);
    std::complex<double> *expected_ptr = (std::complex<double> *)expected.begin();
    for (int i = 0; i < N; i++) {
        expected_ptr[i + 0 * N] = std::complex<double>(1.1, 2.2) + input_ptr[i];
        expected_ptr[i + 1 * N] = std::complex<double>(3.3, 4.4) - input_ptr[i];
        expected_ptr[i + 2 * N] = std::complex<double>(5.5, 6.6) * input_ptr[i];
        //expected_ptr[i+3*N] = std::complex<double>(7.7,8.8) / input_ptr[i];
        for (int j = 0; j < 3; j++) {
            std::complex<double> value = expected_ptr[i + j * N];
            if (std::abs(output(0, i, j) - value.real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << value.real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - value.imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << value.imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}

void test_ops_complex_real() {
    printf("test_ops_complex_real\n");
    Buffer<double> input = gen_buf();
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = ComplexExpr(Expr(0.0), Expr(0.0));
    result(x, 0) = input_complex(x) + Expr(1.2);
    result(x, 1) = input_complex(x) - Expr(3.4);
    result(x, 2) = input_complex(x) * Expr(5.6);
    result(x, 3) = input_complex(x) / Expr(7.8);
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());

    Buffer<double> output = interleaved_output.realize(2, N, 4);

    Buffer<double> expected(2, N, 4);
    std::complex<double> *expected_ptr = (std::complex<double> *)expected.begin();
    for (int i = 0; i < N; i++) {
        expected_ptr[i + 0 * N] = input_ptr[i] + 1.2;
        expected_ptr[i + 1 * N] = input_ptr[i] - 3.4;
        expected_ptr[i + 2 * N] = input_ptr[i] * 5.6;
        expected_ptr[i + 3 * N] = input_ptr[i] / 7.8;
        for (int j = 0; j < 4; j++) {
            std::complex<double> value = expected_ptr[i + j * N];
            if (std::abs(output(0, i, j) - value.real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << value.real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - value.imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << value.imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}
void test_ops_real_complex() {
    printf("test_ops_real_complex\n");
    Buffer<double> input = gen_buf();
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = ComplexExpr(Expr(0.0), Expr(0.0));
    result(x, 0) = Expr(1.2) + input_complex(x);
    result(x, 1) = Expr(3.4) - input_complex(x);
    result(x, 2) = Expr(5.6) * input_complex(x);
    //result(x, 3) = Expr(7.8) / input_complex(x);
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());

    Buffer<double> output = interleaved_output.realize(2, N, 4);

    Buffer<double> expected(2, N, 4);
    std::complex<double> *expected_ptr = (std::complex<double> *)expected.begin();
    for (int i = 0; i < N; i++) {
        expected_ptr[i + 0 * N] = 1.2 + input_ptr[i];
        expected_ptr[i + 1 * N] = 3.4 - input_ptr[i];
        expected_ptr[i + 2 * N] = 5.6 * input_ptr[i];
        //expected_ptr[i+3*N] = 7.8 / input_ptr[i];
        for (int j = 0; j < 3; j++) {
            std::complex<double> value = expected_ptr[i + j * N];
            if (std::abs(output(0, i, j) - value.real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << value.real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - value.imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << value.imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}
void test_assignment_ops_complex_complex() {
    printf("test_assignment_ops_complex_complex\n");
    Buffer<double> input = gen_buf();
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = input_complex(x);
    result(x, 0) += ComplexExpr(Expr(1.1), Expr(2.2));
    result(x, 1) -= ComplexExpr(Expr(3.3), Expr(4.4));
    result(x, 2) *= ComplexExpr(Expr(5.5), Expr(6.6));
    //result(x, 3) /= ComplexExpr(Expr(7.7), Expr(8.8));
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());

    Buffer<double> output = interleaved_output.realize(2, N, 4);

    Buffer<double> expected(2, N, 4);
    std::complex<double> *expected_ptr = (std::complex<double> *)expected.begin();
    for (int i = 0; i < N; i++) {
        expected_ptr[i + 0 * N] = input_ptr[i] + std::complex<double>(1.1, 2.2);
        expected_ptr[i + 1 * N] = input_ptr[i] - std::complex<double>(3.3, 4.4);
        expected_ptr[i + 2 * N] = input_ptr[i] * std::complex<double>(5.5, 6.6);
        //expected_ptr[i+3*N] = input_ptr[i] / std::complex<double>(7.7,8.8);
        for (int j = 0; j < 3; j++) {
            std::complex<double> value = expected_ptr[i + j * N];
            if (std::abs(output(0, i, j) - value.real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << value.real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - value.imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << value.imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}

void test_assignment_ops_complex_real() {
    printf("test_assignment_ops_complex_real\n");
    Buffer<double> input(2, 5);
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();
    for (int i = 0; i < N; i++) {
        std::complex<double> value(1.0 + i, i - 1.0);
        input_ptr[i] = value;
    }

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = input_complex(x);
    result(x, 0) += Expr(1.1);
    result(x, 1) -= Expr(3.3);
    result(x, 2) *= Expr(5.5);
    //result(x, 3) /= Expr(7.7);
    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());

    Buffer<double> output = interleaved_output.realize(2, N, 4);

    Buffer<double> expected(2, N, 4);
    std::complex<double> *expected_ptr = (std::complex<double> *)expected.begin();
    for (int i = 0; i < N; i++) {
        expected_ptr[i + 0 * N] = std::complex<double>(1.0 + i, i - 1.0) + 1.1;
        expected_ptr[i + 1 * N] = std::complex<double>(1.0 + i, i - 1.0) - 3.3;
        expected_ptr[i + 2 * N] = std::complex<double>(1.0 + i, i - 1.0) * 5.5;
        //expected_ptr[i+3*N] = std::complex<double>(1.0 + i, i - 1.0) / 7.7;
        for (int j = 0; j < 3; j++) {
            std::complex<double> value = expected_ptr[i + j * N];
            if (std::abs(output(0, i, j) - value.real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << value.real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - value.imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << value.imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}

void test_helper_funcs() {
    printf("test_helper_funcs\n");
    Buffer<double> input(2, N);
    std::complex<double> *input_ptr = (std::complex<double> *)input.begin();
    for (int i = 0; i < N; i++) {
        std::complex<double> value(1.0 + i, i - 1.0);
        input_ptr[i] = value;
    }

    Var c("c"), x("x"), y("y");
    Func input_clamped = BoundaryConditions::constant_exterior(input, Expr(0.0));
    ComplexFunc input_complex("input_complex");
    input_complex(x) = ComplexExpr(input_clamped(0, x), input_clamped(1, x));
    ComplexFunc result("result");
    result(x, y) = ComplexExpr(Expr(0.0), Expr(0.0));
    result(x, 0) = exp(input_complex(x));
    result(x, 1) = expj(input_clamped(1, x));
    result(x, 2) = -input_complex(x);

    Func interleaved_output("interleaved_output");
    interleaved_output(c, x, y) = select(c == 0, result(x, y).re(), result(x, y).im());
    Buffer<double> output = interleaved_output.realize(2, N, 3);

    for (int i = 0; i < N; i++) {
        std::complex<double> values[3];
        std::complex<double> inputvalue(1.0 + i, i - 1.0);
        values[0] = std::exp(inputvalue);
        values[1] = std::exp(std::complex<double>(0.0, inputvalue.imag()));
        values[2] = -inputvalue;
        for (int j = 1; j < 3; j++) {
            if (std::abs(output(0, i, j) - values[j].real()) > 0.01) {
                std::cout << "Wrong real value for element " << i << "," << j << ". Expected " << values[j].real() << ", got " << output(0, i, j) << endl;
                abort();
            }
            if (std::abs(output(1, i, j) - values[j].imag()) > 0.01) {
                std::cout << "Wrong imaginary value for element " << i << "," << j << ". Expected " << values[j].imag() << ", got " << output(1, i, j) << endl;
                abort();
            }
        }
    }
}

int main(int argc, char **argv) {
    test_io();
    test_ops_complex_complex();
    test_ops_complex_real();
    test_ops_real_complex();
    test_assignment_ops_complex_complex();
    test_assignment_ops_complex_real();
    test_helper_funcs();
    printf("Success!\n");
    return 0;
}
