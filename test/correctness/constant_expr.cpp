#include "Halide.h"
#include <limits>
#include <stdio.h>

#ifdef _MSC_VER
#pragma warning(disable : 4800)  // forcing value to bool 'true' or 'false'
#endif

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
bool bit_flip(T a) {
    return ~a;
}

template<typename T>
bool scalar_from_constant_expr(Expr e, T *val) {
    if (type_of<T>().is_int()) {
        const int64_t *i = as_const_int(e);
        if (!i) return false;
        *val = (T)(*i);
        return true;
    } else if (type_of<T>().is_uint()) {
        const uint64_t *u = as_const_uint(e);
        if (!u) return false;
        *val = (T)(*u);
        return true;
    } else if (type_of<T>().is_float()) {
        const double *f = as_const_float(e);
        if (!f) return false;
        *val = (T)(*f);
        return true;
    } else {
        return false;
    }
}

template<typename T>
void test_expr(T value) {
    Type t = type_of<T>();

    // std::cout << "Test " << t << " = " << value << "\n";

    Expr e = make_const(t, value);
    if (e.type() != t) {
        std::cerr << "constant of type " << t << " returned expr of type " << e.type() << "\n";
        exit(1);
    }
    T nvalue = T(0);
    if (!scalar_from_constant_expr<T>(e, &nvalue)) {
        std::cerr << "constant of type " << t << " failed scalar_from_constant_expr with value " << value << "\n";
        exit(1);
    }
    if (nvalue != value) {
        std::cerr << "Roundtrip failed for type " << t << ": input " << value << " output " << nvalue << "\n";
        exit(1);
    }
}

template<typename T>
void test_expr_range() {
    const T low = std::numeric_limits<T>::lowest();
    const T min = std::numeric_limits<T>::min();
    const T max = std::numeric_limits<T>::max();

    test_expr<T>(0);
    test_expr<T>(1);

    test_expr<T>(low);
    test_expr<T>(min);
    test_expr<T>(max);
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

    // Test various edge cases for int64 and double, since we do extra voodoo to
    // disassemble and reassemble them.
    test_expr<int64_t>(-64);
    test_expr<int64_t>((int64_t)0x000000007fffffff);
    test_expr<int64_t>((int64_t)0x0000000080000000);
    test_expr<int64_t>((int64_t)0x0000000080000001);
    test_expr<int64_t>((int64_t)0x00000000ffffffff);
    test_expr<int64_t>((int64_t)0x00000001ffffffff);
    test_expr<int64_t>((int64_t)0x7fffffff00000000);
    test_expr<int64_t>((int64_t)0x7fffffff80000000);
    test_expr<int64_t>((int64_t)0xffffffff80000000);
    test_expr<int64_t>((int64_t)0xffffffff00000001);
    test_expr<int64_t>((int64_t)0x7FFFFFFFFFFFFFFF);
    test_expr<int64_t>((int64_t)0x8000000000000000);
    test_expr<int64_t>((int64_t)0x8000000000000001);

    test_expr<uint64_t>(-64);
    test_expr<uint64_t>((uint64_t)0x000000007fffffff);
    test_expr<uint64_t>((uint64_t)0x0000000080000000);
    test_expr<uint64_t>((uint64_t)0x0000000080000001);
    test_expr<uint64_t>((uint64_t)0x00000000ffffffff);
    test_expr<uint64_t>((uint64_t)0x00000001ffffffff);
    test_expr<uint64_t>((uint64_t)0x7fffffff00000000);
    test_expr<uint64_t>((uint64_t)0x7fffffff80000000);
    test_expr<uint64_t>((uint64_t)0xffffffff80000000);
    test_expr<uint64_t>((uint64_t)0xffffffff00000001);
    test_expr<uint64_t>((uint64_t)0x7FFFFFFFFFFFFFFF);
    test_expr<uint64_t>((uint64_t)0x8000000000000000);
    test_expr<uint64_t>((uint64_t)0x8000000000000001);

    test_expr<float>(3.141592f);
    test_expr<float>(3.40282e+38f);
    test_expr<float>(3.40282e+38f);

    test_expr<double>(3.1415926535897932384626433832795);
    test_expr<double>(1.79769e+308);
    test_expr<double>(-1.79769e+308);

    printf("Success!\n");
    return 0;
}
