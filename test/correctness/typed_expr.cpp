#include "Halide.h"
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
void check_type(const Expr &e) {
    if (e.type() != type_of<T>()) {
        std::cerr << "constant of type " << type_of<T>() << " returned expr of type " << e.type() << "\n";
        exit(-1);
    }
}

template<typename T>
void test_expr(T value) {
    std::cout << "Test " << type_of<T>() << " = " << (0 + value) << "\n";

    {
        ExprT<T> et(value);
        check_type<T>(et);

        // ExprT<> -> Expr is always OK
        Expr e0 = et;
        check_type<T>(e0);

        Expr e1(et);
        check_type<T>(e1);

        Expr e2(std::move(et));
        check_type<T>(e2);
    }

    {
        ExprT<T> et(value);
        check_type<T>(et);

        // ExprT<int> et_nope = et;  // won't compile, wrong types

        // Cast the type to an int -- is generally ok, will
        // coerce the values as appropriate
        // (except for strings, which fill fail at runtime)
        if (!std::is_same<T, const char *>::value) {
            ExprT<int> et1 = cast<int>(et);
            check_type<int>(et1);
        }

        // Obviously this won't even compile
        // ExprT<int> et2 = et.typed<T>();
        // check_type<int>(et2);

        // Will fail at runtime if et isn't an int32
        if (std::is_same<T, int>::value) {
            ExprT<int> et3 = et.template typed<int>();
            check_type<int>(et3);
        }
    }
}

template<typename T>
void test_expr_range() {
    test_expr<T>((T) 0);
    test_expr<T>((T) 1);
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
    test_expr_range<float16_t>();
    test_expr_range<bfloat16_t>();
    test_expr_range<float>();
    test_expr_range<double>();

    test_expr<const char *>("foo");

    std::cout << "Success!\n";
    return 0;
}
