#include "Halide.h"
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

std::set<int> prefetch_offsets;

int record_prefetch(int offset) {
    prefetch_offsets.insert(offset);
    return 0;
}

// Replaces each (per-cache-line) prefetch of the named buffer with a call that
// records its element offset.
class RecordPrefetches : public Internal::IRMutator {
    using Internal::IRMutator::visit;

    std::string name;

    Internal::Stmt visit(const Internal::Evaluate *op) override {
        const Internal::Call *call = Internal::Call::as_intrinsic(op->value, {Internal::Call::prefetch});
        if (call) {
            const Internal::Variable *base = call->args[0].as<Internal::Variable>();
            if (base && base->name == name) {
                Expr record = Internal::Call::make(Int(32), "record_prefetch",
                                                   {cast<int>(call->args[1])},
                                                   Internal::Call::Extern);
                return Internal::Evaluate::make(record);
            }
        }
        return Internal::IRMutator::visit(op);
    }

public:
    RecordPrefetches(const std::string &name)
        : name(name) {
    }
};

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

    // Prefetching a split Func fetches the bounding box of the region in
    // storage coordinates. g's storage is [xo][y][xi] with xi of extent 8, so
    // (for uint8, with 64-byte cache lines and a 128-byte xo stride) a
    // prefetch of row r touches one cache line at r * 8 + xo * 128 for each xo.
    {
        Func pf_g("pf_g"), pf_f("pf_f");
        Var xo("xo"), xi("xi");
        pf_g(x, y) = cast<uint8_t>(x + 3 * y);
        pf_f(x, y) = pf_g(x, y);
        pf_g.compute_root().split_storage(x, xo, xi, 8).reorder_storage(xi, y, xo);
        pf_f.prefetch(pf_g, y, y, 2);

        Target t = get_jit_target_from_environment();
        Pipeline p(pf_f);
        if (t.arch == Target::X86) {
            p.add_custom_lowering_pass(new RecordPrefetches(pf_g.name()));
            p.set_jit_externs({{"record_prefetch", JITExtern{record_prefetch}}});
            prefetch_offsets.clear();
        }

        const int w = 32, h = 16;
        Buffer<uint8_t> out = p.realize({w, h}, t);
        for (int yy = 0; yy < h; yy++) {
            for (int xx = 0; xx < w; xx++) {
                if (out(xx, yy) != (uint8_t)(xx + 3 * yy)) {
                    printf("Prefetch mismatch at (%d, %d)\n", xx, yy);
                    return 1;
                }
            }
        }

        if (t.arch == Target::X86) {
            std::set<int> expected;
            for (int r = 2; r < h; r++) {
                for (int b = 0; b < w / 8; b++) {
                    expected.insert(r * 8 + b * 128);
                }
            }
            if (prefetch_offsets != expected) {
                printf("Unexpected prefetch offsets:");
                for (int o : prefetch_offsets) {
                    printf(" %d", o);
                }
                printf("\n");
                return 1;
            }
        }
    }

    // Prefetching a ring-buffered Func, with and without split_storage.
    for (bool split : {false, true}) {
        Func producer("pf_ring_producer"), consumer("pf_ring_consumer");
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        Var so("so"), si("si");

        producer(x, y) = x + y;
        consumer(x, y) = producer(x, y) + producer(x + 1, y);
        consumer.compute_root().tile(x, y, xo, yo, xi, yi, 8, 8);
        producer.compute_at(consumer, xo)
            .hoist_storage(consumer, yo)
            .ring_buffer(2);
        if (split) {
            producer.split_storage(x, so, si, 4);
        }
        consumer.prefetch(producer, yi, yi, 2);

        Buffer<int> out = consumer.realize({16, 16});
        for (int yy = 0; yy < out.height(); yy++) {
            for (int xx = 0; xx < out.width(); xx++) {
                if (out(xx, yy) != 2 * (xx + yy) + 1) {
                    printf("Ring prefetch mismatch at (%d, %d)\n", xx, yy);
                    return 1;
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
