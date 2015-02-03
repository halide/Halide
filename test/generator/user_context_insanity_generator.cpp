#include "Halide.h"

namespace {

class UserContextInsanity : public Halide::Generator<UserContextInsanity> {
public:
    ImageParam input{ Float(32), 2, "input" };

    void help(std::ostream &out) override {
        out << "This tests using different user_context pointers from different threads"
            << " and making sure that the right user_contexts make it through the thread"
            << " pool correctly.\n";
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

    bool test() override {
        // This is an aot-only test
        return true;
    }
};

Halide::RegisterGenerator<UserContextInsanity> register_my_gen{"user_context_insanity"};

}  // namespace
