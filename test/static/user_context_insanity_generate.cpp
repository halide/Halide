#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(Float(32), 2);

    Var x, y;

    Func g;
    g(x, y) = input(x, y) * 2;
    g.compute_root();

    Func f;
    f(x, y) = g(x, y);

    f.parallel(y);
    f.trace_stores();
    f.compile_to_file("user_context_insanity", input, user_context_param());
    return 0;
}
