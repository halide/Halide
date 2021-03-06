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
    const map<string, int> &alignments_needed;
    CheckLoadsStoresAligned(const map<string, int> &m)
        : alignments_needed(m) {
    }

    using IRMutator::visit;

    void check_alignment(const string &name, const ModulusRemainder &alignment) {
        auto i = alignments_needed.find(name);
        int expected_alignment = i != alignments_needed.end() ? i->second : 1;
        if (alignment.modulus != expected_alignment) {
            printf("Load/store of %s is %d, expected %d\n",
                   name.c_str(), (int)alignment.modulus, expected_alignment);
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

int test() {
    Var x, y, c;
    ImageParam i1(Int(8), 1);
    ImageParam i2(Int(8), 1);
    ImageParam i3(Int(8), 1);

    Func f("f");
    f(x) = i1(x) + i2(x) + i3(x);

    i1.set_host_alignment(128);
    i1.dim(0).set_min(0);
    i2.set_host_alignment(32);
    i2.dim(0).set_min(0);
    f.output_buffer().set_host_alignment(128);
    f.output_buffer().dim(0).set_min(0);
    f.specialize(is_host_aligned(i3, 4));
    f.specialize_fail("No unaligned loads");
    map<string, int> expected_alignment = {
        {i1.name(), 128},
        {i2.name(), 32},
        {i3.name(), 4},
        {f.name(), 128},
    };

    f.add_custom_lowering_pass(new CheckLoadsStoresAligned(expected_alignment), []() {});
    f.compile_jit();

    printf("Success!\n");
    return 0;
}

int main(int argc, char **argv) {
    return test();
}
