#include <Halide.h>
#include "Serializer.h"
#include "Deserializer.h"
using namespace Halide;

int main(int argc, char **argv) {
    // pipeline with single func
    Halide::Func gradient("gradient_func");
    Halide::Var x, y;
    gradient(x, y) = x + y;
    Halide::Pipeline pipe(gradient);
    Serializer serializer;
    serializer.serialize(pipe, "test.hlb");
    Deserializer deserializer;
    Pipeline p = deserializer.deserialize("test.hlb");
    return 0;
}