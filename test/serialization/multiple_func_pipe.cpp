#include "Halide.h"
using namespace Halide;
using namespace Halide::Internal;

// a simple round trip test
int main(int argc, char **argv) {
    // pipeline with single func
    Halide::Func gradient("gradient_func");
    Halide::Var x, y;
    gradient(x, y) = x + y;
    Halide::Func blurx("blurx_func");
    blurx(x, y) = (gradient(x - 1, y) + gradient(x, y) + gradient(x + 1, y)) / 3;
    Halide::Func blury("blury_func");
    blury(x, y) = (blurx(x, y - 1) + blurx(x, y) + blurx(x, y + 1)) / 3;
    Halide::Pipeline pipe(blury);

    serialize_pipeline(pipe, "multi_func_pipe.hlpipe");
    Pipeline deserialized_pipe = deserialize_pipeline("multi_func_pipe.hlpipe");
    bool result = equal(pipe, deserialized_pipe);

    assert(result == true);
    printf("Success!\n");

    return 0;
}
