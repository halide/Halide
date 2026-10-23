#include "Halide.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Counts vector loads and stores whose index isn't a ramp of scalars or a
// broadcast, i.e. the ones that will be gathers or scatters.
class CountGathers : public IRMutator {
    using IRMutator::visit;

    static bool is_dense(const Expr &index) {
        if (const Ramp *r = index.as<Ramp>()) {
            return r->base.type().is_scalar() && is_const_one(r->stride);
        }
        return index.as<Broadcast>() != nullptr;
    }

    Expr visit(const Load *op) override {
        if (op->type.is_vector() && !is_dense(op->index)) {
            gathers++;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (op->value.type().is_vector() && !is_dense(op->index)) {
            scatters++;
        }
        return IRMutator::visit(op);
    }

public:
    int gathers = 0, scatters = 0;
};

bool check_dense(Func f, const char *name) {
    CountGathers counter;
    f.add_custom_lowering_pass(&counter, nullptr);
    f.compile_jit();
    if (counter.gathers || counter.scatters) {
        printf("%s: expected only dense vector loads and stores, but found %d gathers and %d scatters\n",
               name, counter.gathers, counter.scatters);
        return false;
    }
    return true;
}

int fused_dims() {
    // Fusing the two dimensions of an 8-wide image and vectorizing the fused
    // loop by 16 indexes the input and the output with (xy % 8) + (xy / 8) *
    // stride, which is two dense runs of 8 lanes. The simplifier should spell
    // that as nested ramps, so it lowers to dense loads and stores rather
    // than a gather and a scatter.
    ImageParam in(UInt(8), 2);
    Func f;
    Var x, y, xy;
    f(x, y) = in(x, y) + 1;

    // Constrain the output to whole vectors.
    OutputImageParam out = f.output_buffer();
    out.dim(0).set_bounds(0, 8).dim(1).set_min(0);
    out.dim(1).set_extent((out.dim(1).extent() / 2) * 2);
    in.dim(0).set_bounds(0, 8).dim(1).set_min(0);
    f.bound(x, 0, 8)
        .fuse(x, y, xy)
        .vectorize(xy, 16, TailStrategy::RoundUp);

    if (!check_dense(f, "fused_dims")) {
        return 1;
    }

    Buffer<uint8_t> input(8, 6), output(8, 6);
    input.fill([](int x, int y) { return (uint8_t)(x * 7 + y * 13); });
    in.set(input);
    f.realize(output);
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t correct = input(x, y) + 1;
            if (output(x, y) != correct) {
                printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                return 1;
            }
        }
    }
    return 0;
}

bool is_dense_ramp(const Expr &index) {
    const Ramp *r = index.as<Ramp>();
    return r && r->base.type().is_scalar() && is_const_one(r->stride);
}

int one_dim() {
    // Records of 4 rows of 8 values: row x is at (x / 4) * 32 + (x % 4) * 8.
    // Reading a whole record per vector, starting at a row that's a multiple
    // of 4, indexes the input with (ramp(x8(4k), x8(1), 4) % 4) * 8 + ..., a
    // multiramp that collapses to a single dense ramp of 32 lanes. It should
    // be written as that ramp rather than left as a gather.
    ImageParam in(UInt(8), 1);
    Func f, g;
    Var x, y, xo, xi;
    g(y, x) = in((x / 4) * 32 + (x % 4) * 8 + y) + 1;
    f(y, x) = g(y, x);

    // g is computed per tile of 4 rows, so its loop over them starts at a
    // multiple of 4.
    OutputImageParam out = f.output_buffer();
    out.dim(0).set_bounds(0, 8).dim(1).set_min(0);
    out.dim(1).set_extent((out.dim(1).extent() / 4) * 4);
    in.dim(0).set_min(0);
    f.split(x, xo, xi, 4, TailStrategy::RoundUp).vectorize(y).vectorize(xi);
    g.compute_at(f, xo).vectorize(y).vectorize(x);

    if (!check_dense(f, "one_dim")) {
        return 1;
    }

    const int h = 8;
    Buffer<uint8_t> input(h * 8), output(8, h);
    input.fill([](int i) { return (uint8_t)(i * 7); });
    in.set(input);
    f.realize(output);
    for (int x = 0; x < h; x++) {
        for (int y = 0; y < 8; y++) {
            uint8_t correct = input((x / 4) * 32 + (x % 4) * 8 + y) + 1;
            if (output(y, x) != correct) {
                printf("output(%d, %d) = %d instead of %d\n", y, x, output(y, x), correct);
                return 1;
            }
        }
    }

    // The same index on a store, directly through the simplifier.
    Expr k = Variable::make(Int(32), "k"), c = Variable::make(Int(32), "c");
    Expr nested = Ramp::make(Broadcast::make(k * 4, 8), Broadcast::make(1, 8), 4);
    Expr index = (nested % 4) * 8 + Broadcast::make(Ramp::make(c, 1, 8), 4);
    Stmt store = simplify(Store::make("buf", Broadcast::make(cast<uint8_t>(1), 32), index));
    const Store *st = store.as<Store>();
    if (!st || !is_dense_ramp(st->index)) {
        std::cout << "one_dim: expected a dense store, but got:\n"
                  << store;
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (fused_dims() || one_dim()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
