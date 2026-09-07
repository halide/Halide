#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    Var x{"x"};
    Func undefined_producer{"undefined_producer"};  // never given a definition
    Func consumer{"consumer"};
    consumer(x) = x;

    // An undefined Func has no body to splice in, so eager_inline() rejects it.
    consumer.eager_inline({undefined_producer});

    printf("Success!\n");
    return 0;
}
