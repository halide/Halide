#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h, j;
    Var x, y;       
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x+1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    const char *result_file = "compile_to_lowered_stmt.stmt";
    j.compile_to_lowered_stmt(result_file);

    assert(access("compile_to_lowered_stmt.stmt", F_OK) == 0 && "Output file not created.");

    printf("Success!\n");
    return 0;
}
