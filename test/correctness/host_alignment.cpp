#include "Halide.h"
#include <map>
#include <stdio.h>
#include <string>

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

class CheckLoadsStoresAligned : public IRMutator {
public:
    const map<string, ModulusRemainder> &alignments_needed;
    CheckLoadsStoresAligned(const map<string, ModulusRemainder> &m)
        : alignments_needed(m) {
    }

    using IRMutator::visit;

    void check_alignment(const string &name, const ModulusRemainder &alignment) {
        auto i = alignments_needed.find(name);
        ModulusRemainder expected_alignment =
            i != alignments_needed.end() ? i->second : ModulusRemainder(1, 0);
        if (alignment.modulus != expected_alignment.modulus ||
            alignment.remainder != expected_alignment.remainder) {
            printf("Load/store of %s is (%d, %d), expected (%d, %d)\n",
                   name.c_str(), (int)alignment.modulus, (int)alignment.remainder,
                   (int)expected_alignment.modulus, (int)expected_alignment.remainder);
            abort();
        }
    }

    Expr visit(const Load *op) override {
        check_alignment(op->name, op->alignment);
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        check_alignment(op->name, op->alignment);
        return IRMutator::visit(op);
    }
};

int main(int argc, char **argv) {
    ImageParam i1(Int(8), 1);
    ImageParam i2(Int(8), 1);
    ImageParam i3(Int(8), 1);
    ImageParam i4(Int(8), 1);

    Var x;
    Func f;
    f(x) = i1(x) + i2(x * 2) + i3(x / 2) + i4(x + 1);

    i1.dim(0).set_min(0);
    i2.set_host_alignment(4);
    i2.dim(0).set_min(0);
    i4.set_host_alignment(8);
    i4.dim(0).set_min(0);
    f.output_buffer().set_host_alignment(3);
    f.output_buffer().dim(0).set_min(0);
    f.vectorize(x, 12, TailStrategy::RoundUp);
    f.specialize(is_host_aligned(i3, 4) && i3.dim(0).min() == 0);
    f.specialize_fail("No unaligned loads");

    map<string, ModulusRemainder> expected_alignment = {
        {i1.name(), {1, 0}},
        {i2.name(), {4, 0}},
        {i3.name(), {2, 0}},
        {i4.name(), {4, 1}},
        {f.name(), {3, 0}},
    };
    f.add_custom_lowering_pass(new CheckLoadsStoresAligned(expected_alignment), []() {});
    // Test with NoAsserts to make sure the host alignment asserts are present.
    f.compile_jit(get_jit_target_from_environment().with_feature(Target::NoAsserts));

    printf("Success!\n");
    return 0;
}
