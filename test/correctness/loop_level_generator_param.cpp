#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Remove any "$[0-9]+" patterns in the string.
std::string strip_uniquified_names(const std::string &str) {
    size_t pos = 0;
    std::string result = str;
    while ((pos = result.find("$", pos)) != std::string::npos) {
        int digits = 0;
        while (pos+digits+1 < result.size() && isdigit(result[pos+digits+1])) {
            digits++;
        }
        if (digits > 0) {
            result.replace(pos, 1 + digits, "");
        }
        pos += 1;
    }
    return result;
}

class CheckLoopLevels : public IRVisitor {
public:
    static void lower_and_check(Func outer, const std::string &inner_loop_level, const std::string &outer_loop_level) {
        Module m = outer.compile_to_module({outer.infer_arguments()});
        CheckLoopLevels c(inner_loop_level, outer_loop_level);
        m.functions().front().body.accept(&c);
    }

private:
    CheckLoopLevels(const std::string &inner_loop_level, const std::string &outer_loop_level) :
        inner_loop_level(inner_loop_level), outer_loop_level(outer_loop_level) {}

    using IRVisitor::visit;

    const std::string inner_loop_level, outer_loop_level;
    std::string inside_for_loop;

    void visit(const For *op) override {
        std::string old_for_loop = inside_for_loop;
        inside_for_loop = strip_uniquified_names(op->name);
        IRVisitor::visit(op);
        inside_for_loop = old_for_loop;
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == "sin_f32") {
            _halide_user_assert(inside_for_loop == inner_loop_level)
                << "call sin_f32: expected " << inner_loop_level << ", actual: " << inside_for_loop;
        } else if (op->name == "cos_f32") {
            _halide_user_assert(inside_for_loop == outer_loop_level)
                << "call cos_f32: expected " << outer_loop_level << ", actual: " << inside_for_loop;
        }
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        std::string op_name = strip_uniquified_names(op->name);
        if (op_name == "inner") {
            _halide_user_assert(inside_for_loop == inner_loop_level)
                << "inside_for_loop: expected " << inner_loop_level << ", actual: " << inside_for_loop;
        } else if (op_name == "outer") {
            _halide_user_assert(inside_for_loop == outer_loop_level)
                << "inside_for_loop: expected " << outer_loop_level << ", actual: " << inside_for_loop;
        } else {
            _halide_user_assert(0) << "store at: " << op_name << " inside_for_loop: " << inside_for_loop;
        }
    }
};

Var x{"x"};

class Example : public Generator<Example> {
public:
    GeneratorParam<LoopLevel> inner_compute_at{ "inner_compute_at", LoopLevel::inlined() };
    Output<Func> inner{ "inner", Int(32), 1 };

    void generate() {
        // Use sin() as a proxy for verifying compute_at, since it won't
        // ever be generated incidentally by the lowering code as part of
        // general code structure.
        inner(x) = cast(inner.type(), trunc(sin(x) * 1000.0f));
    }

    void schedule() {
        inner.compute_at(inner_compute_at);
    }
};

}  // namespace

int main(int argc, char **argv) {
    GeneratorContext context(get_jit_target_from_environment());

    {
        // Call GeneratorParam<LoopLevel>::set() with 'root' *before* generate(), then never modify again.
        auto gen = context.create<Example>();
        gen->inner_compute_at.set(LoopLevel::root());
        gen->apply();

        Func outer("outer");
        outer(x) = gen->inner(x) + trunc(cos(x) * 1000.0f);

        CheckLoopLevels::lower_and_check(outer,
            /* inner loop level */ "inner.s0.x",
            /* outer loop level */ "outer.s0.x");
    }

    {
        // Call GeneratorParam<LoopLevel>::set() *before* generate() with undefined Looplevel;
        // then modify that LoopLevel after generate() but before lowering
        LoopLevel inner_compute_at;  // undefined: must set before lowering
        auto gen = context.create<Example>();
        gen->inner_compute_at.set(inner_compute_at);
        gen->apply();

        Func outer("outer");
        outer(x) = gen->inner(x) + trunc(cos(x) * 1000.0f);

        inner_compute_at.set({outer, x});

        CheckLoopLevels::lower_and_check(outer,
            /* inner loop level */ "outer.s0.x",
            /* outer loop level */ "outer.s0.x");
    }

    {
        // Call GeneratorParam<LoopLevel>::set() *after* generate()
        auto gen = context.create<Example>();
        gen->apply();

        Func outer("outer");
        outer(x) = gen->inner(x) + trunc(cos(x) * 1000.0f);

        gen->inner_compute_at.set({outer, x});

        CheckLoopLevels::lower_and_check(outer,
            /* inner loop level */ "outer.s0.x",
            /* outer loop level */ "outer.s0.x");
    }

    {
        // And now, a case that doesn't work:
        // - Call GeneratorParam<LoopLevel>::set() *after* generate()
        // - Then call set(), again, on the local LoopLevel passed previously
        // As expected, the second set() will have no effect.
        auto gen = context.create<Example>();
        gen->apply();

        Func outer("outer");
        outer(x) = gen->inner(x) + trunc(cos(x) * 1000.0f);

        LoopLevel inner_compute_at(LoopLevel::root());
        gen->inner_compute_at.set(inner_compute_at);

        // This has no effect. (If it did, the inner loop level below would be outer.s0.x)
        inner_compute_at.set({outer, x});

        CheckLoopLevels::lower_and_check(outer,
            /* inner loop level */ "inner.s0.x",
            /* outer loop level */ "outer.s0.x");
    }

    printf("Success!\n");
    return 0;
}
