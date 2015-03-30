#include <stdio.h>
#include <limits>
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
bool equals(T a, T b) {
    return a == b;
}

// float64 is never an exact match, since it gets stored as float32.
template<>
bool equals(double a, double b) {
    return fabs(a - b) < 1e10;
}

template<typename T>
bool bit_flip(T a) {
    return ~a;
}

// Not really a useful test for floats; just negate it to avoid compiler error.
template<>
bool bit_flip(float a) {
    return -a;
}

template<>
bool bit_flip(double a) {
    return -a;
}

template<typename T>
void test_expr(T value) {
    Type t = type_of<T>();
    Expr e = scalar_to_constant_expr<T>(value);
    if (e.type() != t) {
        std::cerr << "constant of type " << t << " returned expr of type " << e.type() << "\n";
        exit(-1);
    }
    T nvalue;
    if (!scalar_from_constant_expr<T>(e, &nvalue)) {
        std::cerr << "constant of type " << t << " failed scalar_from_constant_expr with value " << value << "\n";
        exit(-1);
    }
    if (!equals(nvalue, value)) {
        std::cerr << "Roundtrip failed for type " << t << ": input " << value << " output " << nvalue << "\n";
        exit(-1);
    }
    // std::cout << "Test " << t << " = " << value << "\n";
}

template<typename T>
void test_expr_range() {
    const T min = std::numeric_limits<T>::min();
    // Don't bother testing std::numeric_limits<double>::max(),
    // since we know it will fail (since Exprs clamp to float32);
    // rather than special-casing that comparison elsewhere, just
    // limit to the float32 range here.
    const T max = type_of<T>().code == Type::Float ?
                    std::numeric_limits<float>::max() :
                    std::numeric_limits<T>::max();
    const T mid = min + (max - min) / 2;
    test_expr<T>(0);
    test_expr<T>(1);
    test_expr<T>(min);
    test_expr<T>(max);
    test_expr<T>(mid);
    test_expr<T>(mid - 1);
    test_expr<T>(mid + 2);
    test_expr<T>(mid / 3);
    test_expr<T>(mid + mid / 3);
    test_expr<T>(bit_flip<T>(max));
    test_expr<T>(bit_flip<T>(mid));
}

int main(int argc, char **argv) {
    test_expr_range<bool>();
    test_expr_range<uint8_t>();
    test_expr_range<uint16_t>();
    test_expr_range<uint32_t>();
    test_expr_range<int8_t>();
    test_expr_range<int16_t>();
    test_expr_range<int32_t>();
    test_expr_range<int64_t>();
    test_expr_range<uint64_t>();
    test_expr_range<float>();
    test_expr_range<double>();

    printf("Success!\n");
    return 0;
}
