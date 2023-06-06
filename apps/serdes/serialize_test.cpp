#include "Deserializer.h"
#include "Printer.h"
#include "Serializer.h"
#include <Halide.h>
using namespace Halide;

int main(int argc, char **argv) {
    // pipeline with single func
    Halide::Func gradient("gradient_func");
    Halide::Var x, y;
    gradient(x, y) = x + y;
    Halide::Pipeline pipe(gradient);

    // print before serialization
    std::cout << "before serialization\n";
    Printer printer;
    printer.print_pipeline(pipe);

    // serialize and deserialize
    Serializer serializer;
    serializer.serialize(pipe, "test.hlpipe");
    Deserializer deserializer;
    Pipeline p = deserializer.deserialize("test.hlpipe");

    // print after deserialization
    std::cout << "\nafter deserialization\n";
    printer.print_pipeline(p);
    return 0;
}
