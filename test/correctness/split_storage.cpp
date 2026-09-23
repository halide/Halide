#include "Halide.h"
#include <algorithm>
#include <random>
#include <set>
#include <stdio.h>

using namespace Halide;

// Reference value stored by g at (x, y, c). Chosen so that any layout bug
// that aliases two distinct coordinates or drops a coordinate corrupts the
// result.
static int ref_value(int x, int y, int c) {
    return x * 1 + y * 100 + c * 10000 + 7;
}

const int W = 7, H = 5, C = 3;

// Check that realizing a pipeline whose intermediate g has some storage
// schedule produces exactly the reference values.
bool check(const Func &f_in, const std::string &desc) {
    Func f = f_in;
    Buffer<int> out(W, H, C);
    out.fill(-1);
    f.realize(out);
    for (int c = 0; c < C; c++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                if (out(x, y, c) != ref_value(x, y, c)) {
                    printf("Mismatch at (%d, %d, %d): got %d, expected %d\n"
                           "Schedule: %s\n",
                           x, y, c, out(x, y, c), ref_value(x, y, c), desc.c_str());
                    return false;
                }
            }
        }
    }
    return true;
}

// Build g(x,y,c) = ref_value, computed at root so it gets an internal
// allocation, and f = g.
Func make_pipeline(Func &g, Var x, Var y, Var c) {
    g(x, y, c) = x * 1 + y * 100 + c * 10000 + 7;
    Func f;
    f(x, y, c) = g(x, y, c);
    g.compute_root();
    return f;
}

int main(int argc, char **argv) {
    Var x("x"), y("y"), c("c");

    // A few directed cases first.
    {
        Func g("g");
        Func f = make_pipeline(g, x, y, c);
        Var xo("xo"), xi("xi");
        // Tile the x storage into 4-wide strips laid out planar-ish:
        // (xi, y, xo) as innermost-to-outermost.
        g.split_storage(x, xo, xi, 4).reorder_storage(xi, y, xo);
        if (!check(f, "split x by 4, reorder (xi,y,xo)")) {
            return 1;
        }
    }
    {
        Func g("g");
        Func f = make_pipeline(g, x, y, c);
        Var yo("yo"), yi("yi");
        // Split y and align/bound the pieces.
        g.split_storage(y, yo, yi, 2)
            .align_storage(yi, 4)
            .bound_storage(yo, 8)
            .reorder_storage(c, yi, x, yo);
        if (!check(f, "split y by 2, align yi, bound yo, reorder")) {
            return 1;
        }
    }
    {
        Func g("g");
        Func f = make_pipeline(g, x, y, c);
        Var xo("xo"), xi("xi"), xio("xio"), xii("xii");
        // Split twice (split a split piece).
        g.split_storage(x, xo, xi, 4)
            .split_storage(xi, xio, xii, 2)
            .reorder_storage(xii, y, xio, c, xo);
        if (!check(f, "split x by 4 then inner by 2")) {
            return 1;
        }
    }

    // split_storage and ring_buffer add independent storage axes.
    {
        Func producer("producer"), consumer("consumer");
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        Var so("so"), si("si");

        producer(x, y) = x + y;
        consumer(x, y) = producer(x, y);
        consumer.compute_root().tile(x, y, xo, yo, xi, yi, 8, 8);
        producer.compute_at(consumer, xo)
            .hoist_storage(consumer, yo)
            .ring_buffer(2)
            .split_storage(x, so, si, 4);

        Buffer<int> out = consumer.realize({16, 16});
        for (int yy = 0; yy < out.height(); yy++) {
            for (int xx = 0; xx < out.width(); xx++) {
                if (out(xx, yy) != xx + yy) {
                    printf("Ring buffer mismatch at (%d, %d): got %d, expected %d\n",
                           xx, yy, out(xx, yy), xx + yy);
                    return 1;
                }
            }
        }
    }

    // split_storage on one axis combined with fold_storage on a different axis.
    // g slides over y (a two-tap stencil), so its y storage can be folded into a
    // circular buffer, while its x storage is split into tiles. Storage folding
    // errors if a requested fold is not applied, so reaching a correct result
    // here verifies that folding an unsplit axis of a split Func works.
    {
        Func g("g"), fold_f("fold_f");
        g(x, y) = x * 2 + y * 3 + 1;
        fold_f(x, y) = g(x, y) + g(x, y + 1);
        Var xo("xo"), xi("xi");
        g.compute_at(fold_f, y)
            .store_root()
            .split_storage(x, xo, xi, 4)
            .reorder_storage(xi, y, xo)
            .fold_storage(y, 2);

        const int fw = 30, fh = 20;
        Buffer<int> out = fold_f.realize({fw, fh});
        for (int yy = 0; yy < fh; yy++) {
            for (int xx = 0; xx < fw; xx++) {
                int ref = (xx * 2 + yy * 3 + 1) + (xx * 2 + (yy + 1) * 3 + 1);
                if (out(xx, yy) != ref) {
                    printf("Mismatch at (%d, %d): got %d, expected %d\n"
                           "Schedule: split x, fold y\n",
                           xx, yy, out(xx, yy), ref);
                    return 1;
                }
            }
        }
    }

    // An async producer with an explicit fold over y and a footprint that
    // can't be statically proven monotonic, inside an outer loop over c. This
    // uses the dynamically-tracked fold, whose counters are stepped back by the
    // folded extent after each iteration of c. x is split, so the folded axis
    // is not at its arg position in the buffer. The sizes make the wrong buffer
    // dimension's extent (that of xo) too small to step the counters back far
    // enough.
    {
        Func producer("async_producer"), consumer("async_consumer");
        Var c("c"), xo("xo"), xi("xi");
        Param<int> stride("stride");
        producer(x, y, c) = x + y + c;
        consumer(x, y, c) = producer(x - 1, y * stride, c) + producer(x + 1, y * stride + 1, c);
        consumer.compute_root();
        producer.store_root()
            .compute_at(consumer, y)
            .split_storage(x, xo, xi, 4)
            .fold_storage(y, 16)
            .async();

        stride.set(1);
        const int w = 8, h = 40, nc = 3;
        Buffer<int> out = consumer.realize({w, h, nc});
        for (int cc = 0; cc < nc; cc++) {
            for (int yy = 0; yy < h; yy++) {
                for (int xx = 0; xx < w; xx++) {
                    int ref = (xx - 1 + yy + cc) + (xx + 1 + yy + 1 + cc);
                    if (out(xx, yy, cc) != ref) {
                        printf("Async fold mismatch at (%d, %d, %d): got %d, expected %d\n",
                               xx, yy, cc, out(xx, yy, cc), ref);
                        return 1;
                    }
                }
            }
        }
    }

    // Randomized fuzzing over combinations of storage directives.
    uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
    printf("Fuzzing split_storage with seed %u\n", seed);
    std::mt19937 rng(seed);

    const int num_trials = 300;
    for (int trial = 0; trial < num_trials; trial++) {
        Func g("g");
        Func f = make_pipeline(g, x, y, c);

        // The set of storage axes currently available to schedule, and those
        // with an alignment or bound, which can't be split.
        std::vector<Var> axes = {x, y, c};
        std::set<std::string> configured;
        std::string desc = "seed " + std::to_string(seed) + " trial " + std::to_string(trial) + ":";

        int fresh = 0;
        int num_ops = 1 + rng() % 6;
        for (int op = 0; op < num_ops; op++) {
            int choice = rng() % 4;
            if (choice == 0 && axes.size() < 6) {
                // split_storage
                std::vector<int> splittable;
                for (int i = 0; i < (int)axes.size(); i++) {
                    if (!configured.count(axes[i].name())) {
                        splittable.push_back(i);
                    }
                }
                if (splittable.empty()) {
                    continue;
                }
                int idx = splittable[rng() % splittable.size()];
                int factor = 2 + rng() % 3;  // 2..4
                Var outer("so" + std::to_string(fresh));
                Var inner("si" + std::to_string(fresh));
                fresh++;
                Var old = axes[idx];
                g.split_storage(old, outer, inner, factor);
                axes.erase(axes.begin() + idx);
                axes.push_back(outer);
                axes.push_back(inner);
                desc += " split(" + old.name() + "," + std::to_string(factor) + ")";
            } else if (choice == 1 && axes.size() >= 2) {
                // reorder_storage of two distinct axes
                int a = rng() % axes.size();
                int b = rng() % axes.size();
                if (a == b) {
                    continue;
                }
                g.reorder_storage(axes[a], axes[b]);
                desc += " reorder(" + axes[a].name() + "," + axes[b].name() + ")";
            } else if (choice == 2) {
                // align_storage
                int idx = rng() % axes.size();
                int align = 1 << (1 + rng() % 3);  // 2,4,8
                g.align_storage(axes[idx], align);
                configured.insert(axes[idx].name());
                desc += " align(" + axes[idx].name() + "," + std::to_string(align) + ")";
            } else {
                // bound_storage with a bound that is always large enough
                // (every axis extent here is <= max(W,H,C) <= 7).
                int idx = rng() % axes.size();
                g.bound_storage(axes[idx], 8);
                configured.insert(axes[idx].name());
                desc += " bound(" + axes[idx].name() + ",8)";
            }
        }

        if (!check(f, desc)) {
            printf("Rerun with: %s %u\n", argv[0], seed);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
