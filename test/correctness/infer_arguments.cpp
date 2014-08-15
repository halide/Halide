#include <stdio.h>
#include <Halide.h>
#include <algorithm>

using namespace Halide;

int main(int argc, char **argv) {

    ImageParam input1(UInt(8), 3, "input1");
    ImageParam input2(UInt(8), 2, "input2");
    Param<int32_t> height("height");
    Param<int32_t> width("width");
    Param<uint8_t> thresh("thresh");
    Param<float> frac("frac");

    Var x("x"), y("y"), c("c");

    Func f("f");
    f(x, y, c) = frac * (input1(clamp(x, 0, height), clamp(y, 0, width), c) +
                    min(thresh, input2(x, y)));

#define EXPECT(expect, actual) \
    if (expect != actual) { printf("Failure, expected %s\n", #expect); return -1; }

    std::vector<Argument> args = f.infer_arguments();
    EXPECT(6, args.size());

    EXPECT("input1", args[0].name);
    EXPECT("input2", args[1].name);
    EXPECT("frac", args[2].name);
    EXPECT("height", args[3].name);
    EXPECT("thresh", args[4].name);
    EXPECT("width", args[5].name);

    EXPECT(true, args[0].is_buffer);
    EXPECT(true, args[1].is_buffer);
    EXPECT(false, args[2].is_buffer);
    EXPECT(false, args[3].is_buffer);
    EXPECT(false, args[4].is_buffer);
    EXPECT(false, args[5].is_buffer);

    EXPECT(Type::Float, args[2].type.code);
    EXPECT(Type::Int, args[3].type.code);
    EXPECT(Type::UInt, args[4].type.code);
    EXPECT(Type::Int, args[5].type.code);

    EXPECT(32, args[2].type.bits);
    EXPECT(32, args[3].type.bits);
    EXPECT(8, args[4].type.bits);
    EXPECT(32, args[5].type.bits);

    printf("Success!\n");
    return 0;

}
