// A cascade of second order IIR filter sections over many audio channels -
// the recurrence scipy.signal.sosfilt runs. Each section filters the
// previous section's output, with zero initial state.
//
// Written two ways. As inductive Funcs the whole cascade fuses into one
// streaming pass: the consumer's loop over samples slides every section's
// two-sample window along with it, so each section's state stays in
// registers and the signal crosses memory once in each direction. As update
// definitions over an RDom each section owns its walk and must be computed
// whole before the next section reads it, which costs a pass over the
// signal per section - and the signal is chosen not to fit in cache.

#include "Halide.h"

using namespace Halide;

namespace {

class Biquads : public Halide::Generator<Biquads> {
public:
    // How many second order sections the cascade runs, over how many
    // channels.
    GeneratorParam<int> sections{"sections", 8};
    GeneratorParam<int> channels{"channels", 32};
    enum class ScanForm { Inductive,
                          RDom };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"rdom", ScanForm::RDom}}};
    // Whether blocks of channels spread across cores.
    GeneratorParam<bool> par{"par", false};

    // Channel by sample.
    Input<Buffer<float, 2>> x{"x"};
    // Coefficient by section: b0 b1 b2 a0 a1 a2, with a0 already one -
    // scipy's sos layout.
    Input<Buffer<float, 2>> sos{"sos"};
    Output<Buffer<float, 2>> y{"y"};

    void generate() {
        Var c("c"), n("n");
        const int N = sections;

        Func xin("xin");
        xin(c, n) = x(c, n);

        RDom r(1, x.dim(1).extent() - 1, "r");
        std::vector<Func> ys;
        Func prev = xin;
        for (int k = 0; k < N; k++) {
            Expr b0 = sos(0, k), b1 = sos(1, k), b2 = sos(2, k);
            Expr a1 = sos(4, k), a2 = sos(5, k);
            const bool last = k == N - 1;
            // Direct form one: the taps of the previous section's output,
            // minus the feedback of this one's.
            if (inductive()) {
                Func yk = Func(Float(32), "y" + std::to_string(k));
                // The clamps keep the rolling window's warm-up rows, which
                // the boundary selects discard, from reaching outside the
                // input.
                yk(c, n) =
                    b0 * prev(c, max(n, 0)) +
                    select(n < 1, 0.f,
                           likely(b1 * prev(c, max(n - 1, 0)) -
                                  a1 * yk(c, n - 1))) +
                    select(n < 2, 0.f,
                           likely(b2 * prev(c, max(n - 2, 0)) -
                                  a2 * yk(c, n - 2)));
                ys.push_back(yk);
                prev = yk;
            } else {
                // The pure definition is the feed-forward half, which is
                // parallel; the update folds the feedback in place, walking
                // the samples in order.
                Func yk = last ? Func(y) : Func(Float(32), "y" + std::to_string(k));
                yk(c, n) = b0 * prev(c, n) +
                           select(n < 1, 0.f, b1 * prev(c, max(n - 1, 0))) +
                           select(n < 2, 0.f, b2 * prev(c, max(n - 2, 0)));
                yk(c, r) = yk(c, r) - a1 * yk(c, r - 1) -
                           select(r < 2, 0.f, a2 * yk(c, max(r - 2, 0)));
                if (!last) {
                    ys.push_back(yk);
                }
                prev = yk;
            }
        }
        if (inductive()) {
            y(c, n) = prev(c, n);
        }

        // ---------------- Schedule ----------------

        const int VEC = natural_vector_size<float>();
        Var co("co"), ci("ci");
        if (inductive()) {
            // One serial walk over the samples per block of channels, a
            // vector of channels wide. Every section computes its next
            // sample inside that walk, into a four-deep rolling window
            // that never leaves cache.
            // All the channel vectors advance together through one serial
            // walk over the samples: their recurrences are independent, so
            // interleaving them hides the filter's latency chain.
            if (par) {
                // Each core owns a pair of channel vectors and walks its
                // share of the signal independently.
                Var coo("coo"), coi("coi");
                y.split(c, co, ci, VEC)
                    .split(co, coo, coi, 2)
                    .reorder(ci, coi, n, coo)
                    .vectorize(ci)
                    .unroll(coi)
                    .parallel(coo);
                for (Func f : ys) {
                    f.store_at(y, coo)
                        .compute_at(y, coi)
                        .fold_storage(n, 4)
                        .vectorize(c, VEC);
                }
            } else {
                y.split(c, co, ci, VEC)
                    .reorder(ci, co, n)
                    .vectorize(ci)
                    .unroll(co);
                for (Func f : ys) {
                    f.store_root()
                        .compute_at(y, co)
                        .fold_storage(n, 4)
                        .vectorize(c, VEC);
                }
            }
        } else {
            for (int k = 0; k < N; k++) {
                Func f = k == N - 1 ? Func(y) : ys[k];
                if (k < N - 1) {
                    f.compute_root();
                }
                f.split(c, co, ci, VEC)
                    .reorder(ci, n, co)
                    .vectorize(ci);
                // The channel blocks' walks are independent, so the
                // parallel loop must sit outside the serial one.
                f.update()
                    .split(c, co, ci, VEC)
                    .reorder(ci, r, co)
                    .vectorize(ci);
                if (par) {
                    f.parallel(co);
                    f.update().parallel(co);
                }
            }
        }

        x.dim(0).set_bounds(0, channels);
        y.dim(0).set_bounds(0, channels);
        y.dim(1).set_bounds(0, x.dim(1).extent());
    }

    bool inductive() const {
        return scan == ScanForm::Inductive;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Biquads, biquads)
