#include "Halide.h"

class Float16T : public Halide::Generator<Float16T> {
public:
    Func build() {
        // Currently the float16 aot test just exercises the
        // runtime. More interesting code may go here in the future.
        Var x;
        Func f;
        f(x) = x;
        return f;
    }
};

HALIDE_REGISTER_GENERATOR(Float16T, float16_t)
