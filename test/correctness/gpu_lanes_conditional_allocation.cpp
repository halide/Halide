#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// An allocation inside a serial loop within a loop over gpu lanes must
// stay inside that loop when warp shuffles are lowered: its condition
// (here, a GuardWithIf tail's) refers to the serial loop's variable.
// https://github.com/halide/Halide/issues/9571

namespace {

// Checks that within loops over gpu lanes, every variable is used within
// the scope of its definition.
class CheckLaneLoops : public IRVisitor {
    using IRVisitor::visit;

    Scope<> defined;
    bool in_lane_loop = false;

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
        ScopedValue<bool> lanes(in_lane_loop, in_lane_loop || op->for_type == ForType::GPULane);
        op->body.accept(this);
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

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    bool can_run = t.has_feature(Target::Metal) ||
                   t.get_cuda_capability_lower_bound() >= 50;

    // 40 blocks of 8 values per row: the split of r.y by 32 has a tail.
    const int width = 8 * 40, height = 4;
    Buffer<float> in(width, height);
    in.for_each_element([&](int x, int y) { in(x, y) = (x * 7 + y * 13) % 41 - 20; });

    Var x("x"), k("k"), u("u");
    RDom r(0, 8, 0, in.width() / 8, "r");
    Func h("h"), dot("dot"), out("out");
    h(k, x) = in(k, x) * 2.f;
    dot(x) = 0.f;
    dot(x) += h(r.x + 8 * r.y, x);
    out(x) = dot(x);

    // Each lane accumulates a strided subset of the blocks, computing h
    // per block in registers.
    RVar ro("ro"), ri("ri");
    out.gpu_blocks(x);
    Func intm = dot.update().split(r.y, ro, ri, 32, TailStrategy::GuardWithIf).rfactor(ri, u);
    dot.compute_at(out, x);
    intm.compute_at(out, x).gpu_lanes(u);
    intm.update().reorder(r.x, ro, u).gpu_lanes(u);
    h.compute_at(intm, ro).bound_storage(k, 8);

    std::vector<std::string> failures;
    out.add_custom_lowering_pass(new CheckLaneLoopsPass(failures));
    out.compile_to_module({}, "out", Target("x86-64-linux-cuda-cuda_capability_50"));
    out.clear_custom_lowering_passes();
    for (const auto &failure : failures) {
        printf("%s\n", failure.c_str());
    }
    if (!failures.empty()) {
        return 1;
    }

    if (can_run) {
        Buffer<float> result = out.realize({height}, t);
        for (int y = 0; y < height; y++) {
            float correct = 0.f;
            for (int x = 0; x < width; x++) {
                correct += in(x, y) * 2.f;
            }
            if (result(y) != correct) {
                printf("out(%d) = %f instead of %f\n", y, result(y), correct);
                return 1;
            }
        }
    } else {
        printf("Not running on a GPU: Metal, or CUDA with capability 5.0 or greater, required.\n");
    }

    printf("Success!\n");
    return 0;
}
