#include "Halide.h"

namespace {

class UserContextInsanity : public Halide::Generator<UserContextInsanity> {
public:
    ImageParam input{ Float(32), 2, "input" };
    Param<void *> user_context{ "__user_context" };

    static std::string name() {
        return "user_context_insanity";
    }

    Func build() override {
        Var x, y;

        Func g;
        g(x, y) = input(x, y) * 2;
        g.compute_root();

        Func f;
        f(x, y) = g(x, y);

        f.parallel(y);
        f.trace_stores();
        return f;
    }
};

Halide::RegisterGenerator<UserContextInsanity> register_my_gen;

}  // namespace
