#include "Halide.h"
using namespace Halide;

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

    // serialize and deserialize
    Serializer serializer;
    serializer.serialize(pipe, "test.hlpipe");
    Deserializer deserializer;
    Pipeline p = deserializer.deserialize("test.hlpipe");

    return 0;
}
