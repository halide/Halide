#include "Halide.h"

namespace {

class UserContext : public Halide::Generator<UserContext> {
public:
    ImageParam input{ Int(32), 2, "input" };
    Param<void *> user_context{ "__user_context" };

    static std::string name() {
        return "user_context";
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

Halide::RegisterGenerator<UserContext> register_my_gen;

}  // namespace
