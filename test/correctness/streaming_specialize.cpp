#include <Halide.h>
using namespace Halide;
using namespace Halide::Internal;

// Each specialization is its own Definition and may independently request
// streaming (see Stage::stream_stores); a Stage's Definition can therefore
// be expanded, by lowering, into more than one Provide node (one per
// specialization, plus the base/default case). The decision of which resulting
// Store is streaming -- and whether a fence is needed at all -- must track the
// exact Definition each Provide came from.
int main(int argc, char **argv) {
    ImageParam in(Int(32), 1, "in");
    Expr cond = in.width() >= 8;

    // Only the (vectorized) specialization streams; the base case does not.
    // The specialization also vectorizes so that the two branches remain
    // distinguishable all the way through lowering: if they differed only
    // in stream_stores(), which has no effect until Store nodes are built
    // during storage flattening, an earlier generic simplification pass
    // would see two structurally-identical branches and collapse them into
    // one before we could observe them separately.
    Func f("stream_specialize");
    Var x;
    f(x) = in(x);
    Stage spec = f.specialize(cond);
    spec.vectorize(x, 8);
    spec.stream_stores();
    f.compute_root();

    // Verify the lowered structure directly (rather than via realize()),
    // since binding a concrete buffer to `in` before lowering would let the
    // specialization condition be proven statically, collapsing the two
    // branches into one before we could observe them separately.
    int stores_streaming = 0, stores_ordinary = 0, fences = 0;
    Module m = f.compile_to_module({in}, f.name());
    for (const auto &fn : m.functions()) {
        visit_with(fn.body, [&](auto *self, const auto *op) {
            if constexpr (std::is_same_v<decltype(op), const Store *>) {
                if (op->name.rfind("stream_specialize", 0) == 0) {
                    ++(op->is_streaming ? stores_streaming : stores_ordinary);
                }
            } else if constexpr (std::is_same_v<decltype(op), const Call *>) {
                if (op->is_intrinsic(Call::stream_store_fence)) {
                    ++fences;
                }
            }
            self->visit_base(op);
        });
    }
    if (stores_streaming == 0 || stores_ordinary == 0) {
        std::fprintf(stderr, "Expected both a streaming store (from the vectorized "
                             "specialization) and an ordinary store (from the base "
                             "case) for stream_specialize, found streaming=%d ordinary=%d\n",
                     stores_streaming, stores_ordinary);
        return 1;
    }
    if (fences != 1) {
        std::fprintf(stderr, "Expected exactly 1 streaming store fence for "
                             "stream_specialize (covering both specialization "
                             "branches), found %d\n",
                     fences);
        return 1;
    }

    // Correctness check: realize with concrete inputs on both sides of the
    // specialization condition.
    for (int width : {4, 16}) {
        Buffer<int32_t> buf(width);
        for (int i = 0; i < width; i++) {
            buf(i) = i;
        }
        in.set(buf);
        Buffer<int32_t> result = f.realize({width});
        for (int i = 0; i < width; i++) {
            if (result(i) != i) {
                std::fprintf(stderr, "stream_specialize: incorrect result at %d (width %d): %d\n",
                             i, width, result(i));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
