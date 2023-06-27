#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Func gradient("gradient_func");
    Var x, y;
    gradient(x, y) = x + y;
    Pipeline pipe(gradient);

    Serializer serializer;
    serializer.serialize(pipe, "single_func_pipe.hlpipe");
    Deserializer deserializer;
    Pipeline deserialized_pipe = deserializer.deserialize("single_func_pipe.hlpipe");
    bool result = equal(pipe, deserialized_pipe);

    assert(result == true);
    printf("Success!\n");
    return 0;
}