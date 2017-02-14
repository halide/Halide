#include "Halide.h"

namespace {

using namespace Halide;

Func build_simple_func(int extra) {
    Var x, y;
    Func f;
    f(x, y) = cast<int32_t>(x + y + extra);
    return f;
}

class OutputAssign : public Halide::Generator<OutputAssign> {
public:
    Output<Func> output{ "output", Int(32), 2 };
    Output<Func[2]> output_array{ "output_array", Int(32), 2 }; 

    void generate() {
        output = build_simple_func(0);
        for (int i = 0; i < 2; ++i) {
            output_array[i] = build_simple_func(i + 1);
        }
    }

     void schedule() {
        // nothing
     }
};

HALIDE_REGISTER_GENERATOR(OutputAssign, "output_assign")

}  // namespace
