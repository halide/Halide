#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Func add2(Func in) {
    Func a("ext");
    a(_) = in(_) + 2;
    return a;
}

int main(int argc, char **argv) {

    Func ext1("ext"), ext2("ext");
    Var x("x");

    ext1(x) = x + 1;
    ext2(x) = x + 2;

    assert(ext1.name() != ext2.name() && "Programmer-specified function names have not been made unique!");

    Buffer<int> out1 = ext1.realize(10);
    Buffer<int> out2 = ext2.realize(10);

    for (int i = 0; i < 10; i++) {
        assert(out1(i) == i + 1 && "Incorrect result from call to ext1");
    }

    for (int i = 0; i < 10; i++) {
        assert(out2(i) == i + 2 && "Incorrect result from call to ext2");
    }

    Func out1_as_func(out1);
    Func ext3 = add2(out1_as_func);

    Buffer<int> out3 = ext3.realize(10);

    for (int i = 0; i < 10; i++) {
        assert(out3(i) == i + 3 && "Incorrect result from call to add2");
    }

    printf("Success!\n");

    return 0;

}
