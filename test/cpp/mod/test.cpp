#include "Halide.h"

using namespace Halide;

template<typename T>
bool test() {
    Var x;
    Func f;
    f(x) = cast<T>(x) % 2;
    
    Image<T> im = f.realize(16);

    for (int i = 0; i < 16; i++) {
        printf("%f ", (double)(im(i)));
        if (im(i) != (T)(i%2)) return false;
    }
    printf("\n");

    return true;
}

int main(int argc, char **argv) {

    if (test<float>() &&
        test<double>() &&
        test<int32_t>() &&
        test<uint32_t>() &&
        test<int16_t>() &&
        test<uint16_t>() &&
        test<int8_t>() &&
        test<uint8_t>()) {
        printf("Success!\n");
        return 0;
    }

    printf("Failure!\n");
    return -1;
}
