#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename T>
bool test_type() {
    Type t = type_of<T>();
    Func f;
    Var x;
    f(x) = cast<T>(1);
    Buffer<T> im = f.realize({10});

    if (f.value().type() != t) {
        std::cout << "Function was defined with type " << t << " but has type " << f.value().type() << "\n";
        return false;
    }

    Expr add_one = im(_) + 1;
    if (add_one.type() != t) {
        std::cout << "Add 1 changed type from " << t << " to " << add_one.type() << "\n";
        return false;
    }

    Expr one_add = 1 + im(_);
    if (one_add.type() != t) {
        std::cout << "Pre-add 1 changed type from " << t << " to " << one_add.type() << "\n";
        return false;
    }

    /*
      The following will indeed change the type, because we don't do early constant folding
    Expr add_exp = im() + (Expr(1) + 1);
    if (add_exp.type() != t) {
        std::cout << "Add constant expression changed type from " << t << " to " << add_exp.type() << "\n";
    }
    */

    return true;
}

int main(int argc, char **argv) {
    bool ok = true;

    ok = ok && test_type<uint8_t>();
    ok = ok && test_type<uint16_t>();
    ok = ok && test_type<uint32_t>();
    ok = ok && test_type<int8_t>();
    ok = ok && test_type<int16_t>();
    ok = ok && test_type<int32_t>();
    ok = ok && test_type<float>();
    ok = ok && test_type<double>();

    if (ok) {
        printf("Success!\n");
        return 0;
    } else {
        return 1;
    }
}
