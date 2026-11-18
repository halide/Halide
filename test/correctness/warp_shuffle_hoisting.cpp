#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// Warp shuffles must be executed by every lane in the warp, so lowering
// hoists them out of if statements that only some of the lanes execute,
// such as the one that masks off the lanes past the end of a gpu_lanes
// loop narrower than the warp. Anything inside the if that a shuffle
// depends on must be hoisted with it, before it, and only if it doesn't
// depend on anything stored inside the if.

namespace {

// Checks that within loops over gpu lanes, every variable is used within
// the scope of its definition, and that no buffer allocated inside the
// loop over lanes is loaded from before it has been stored to.
class CheckLaneLoops : public IRVisitor {
    using IRVisitor::visit;

    Scope<> defined;
    bool in_lane_loop = false;
    std::set<std::string> allocated, stored;

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
        op->value.accept(this);
        ScopedBinding<> bind(defined, op->name);
        op->body.accept(this);
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

    void visit(const For *op) override {
        op->min.accept(this);
        op->max.accept(this);
        ScopedBinding<> bind(defined, op->name);
        if (op->for_type == ForType::GPULane) {
            in_lane_loop = true;
            op->body.accept(this);
            in_lane_loop = false;
            allocated.clear();
            stored.clear();
        } else {
            op->body.accept(this);
        }
    }

    void visit(const Variable *op) override {
        if (in_lane_loop &&
            !op->param.defined() &&
            !op->image.defined() &&
            !op->reduction_domain.defined() &&
            !defined.contains(op->name)) {
            failures.push_back(op->name + " is used outside the scope of its definition");
        }
    }

    void visit(const Allocate *op) override {
        if (in_lane_loop) {
            allocated.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Load *op) override {
        if (in_lane_loop && allocated.count(op->name) && !stored.count(op->name)) {
            failures.push_back(op->name + " is loaded from before it is stored to");
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        stored.insert(op->name);
    }

public:
    std::vector<std::string> failures;
};

// A custom lowering pass that runs CheckLaneLoops. Custom lowering passes
// run after warp shuffles have been lowered, and before the GPU kernels
// are compiled.
class CheckLaneLoopsPass : public IRMutator {
public:
    std::vector<std::string> &failures;

    CheckLaneLoopsPass(std::vector<std::string> &failures)
        : failures(failures) {
    }

    Stmt mutate(const Stmt &s) override {
        CheckLaneLoops check;
        s.accept(&check);
        failures.insert(failures.end(), check.failures.begin(), check.failures.end());
        // We only want to inspect the lowered kernels, so drop them rather
        // than compiling them, which would need the device backend.
        return Evaluate::make(0);
    }

    using IRMutator::mutate;
};

bool check_lowering(Func f, const std::string &name) {
    std::vector<std::string> failures;
    f.add_custom_lowering_pass(new CheckLaneLoopsPass(failures));
    f.compile_to_module({}, name, Target("x86-64-linux-cuda-cuda_capability_50"));
    f.clear_custom_lowering_passes();
    for (const auto &failure : failures) {
        printf("%s: %s\n", name.c_str(), failure.c_str());
    }
    return failures.empty();
}

bool check_output(const Buffer<int> &out, const std::function<int(int, int)> &correct) {
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            if (out(x, y) != correct(x, y)) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct(x, y));
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    bool can_run = t.has_feature(Target::Metal) ||
                   t.get_cuda_capability_lower_bound() >= 50;

    const int width = 48, height = 4;

    // A function of 32 lanes, stored in registers, and a pair of
    // shuffles of it from a consumer over 24 lanes, which is masked off
    // in the last 8 lanes of the warp.
    Var x("x"), y("y"), xo("xo"), xi("xi");
    auto make_pipeline = [&](Func a, Func b, const Expr &idx) {
        a(x, y) = x * 3 + y;
        Func g("g");
        g(x, y) = a(x % 32, y) + 7 * a((x + 1) % 32, y);
        b(x, y) = g(idx, y);
        b.split(x, xo, xi, 24, TailStrategy::RoundUp)
            .gpu_blocks(xo, y)
            .gpu_lanes(xi);
        a.compute_at(b, xo)
            .bound_extent(x, 32)
            .gpu_lanes(x)
            .store_in(MemoryType::Register);
    };

    auto a_ref = [](int x, int y) { return x * 3 + y; };
    auto g_ref = [&](int x, int y) {
        auto mod = [](int x) { return ((x % 32) + 32) % 32; };
        return a_ref(mod(x), y) + 7 * a_ref(mod(x + 1), y);
    };

    {
        // The shuffles' source lane depends on a value computed inside
        // the if, which must be hoisted before them.
        Func a("a"), b("b");
        make_pipeline(a, b, x * x + y);

        if (!check_lowering(b, "shuffle_of_let")) {
            return 1;
        }

        if (can_run) {
            Buffer<int> out = b.realize({width, height}, t);
            if (!check_output(out, [&](int x, int y) { return g_ref(x * x + y, y); })) {
                return 1;
            }
        }
    }

    {
        // The shuffles' source lane depends on a value loaded from a
        // buffer that is stored to inside the if. The load can't be
        // hoisted above the store.
        Buffer<int> in(width, height);
        in.for_each_element([&](int x, int y) { in(x, y) = (x * 7 + y * 13) % 41 - 20; });

        Func a("a"), b("b"), c("c");
        c(x, y) = in(x, y) * 5 + y;
        make_pipeline(a, b, c(x, y));
        c.compute_at(b, xi);

        if (!check_lowering(b, "shuffle_of_value_stored_in_if")) {
            return 1;
        }

        if (can_run) {
            Buffer<int> out = b.realize({width, height}, t);
            if (!check_output(out, [&](int x, int y) { return g_ref(in(x, y) * 5 + y, y); })) {
                return 1;
            }
        }
    }

    if (!can_run) {
        printf("Not running on a GPU: Metal, or CUDA with capability 5.0 or greater, required.\n");
    }

    printf("Success!\n");
    return 0;
}
