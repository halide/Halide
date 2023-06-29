#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Func gradient("gradient_func");
    Var x, y;
    gradient(x, y) = x + y;
    gradient.compute_root();
    Pipeline pipe(gradient);

    serialize_pipeline(pipe, "single_func_pipe.hlpipe");
    Pipeline deserialized_pipe = deserialize_pipeline("single_func_pipe.hlpipe");
    bool result = equal(pipe, deserialized_pipe);

    assert(result == true);
    printf("Success!\n");
    return 0;
}