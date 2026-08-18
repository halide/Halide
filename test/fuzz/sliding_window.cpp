#include "Halide.h"
#include "fuzz_helpers.h"
#include <iostream>
#include <random>
#include <sstream>

namespace {

using namespace Halide;

// Configuration settings. If you find a failure, you can progressively simplify
// the IR by turning things on and off.

// We want large pipelines to get into complex situations, but small
// pipelines so that we can test lots of them and so that the
// failures are understandable by humans.
constexpr int num_stages = 5;

// None of these configuration options should change the number of calls to the
// rng, or else you can't progressively simplify a repro.
constexpr int size = 15;
constexpr int split_factor = 4;
constexpr TailStrategy output_tail_strategies[] =
    // Only strategies that never store outside the region required. The
    // overcomputing ones can clobber values a sliding window still needs.
    // See https://github.com/halide/Halide/issues/7819
    {TailStrategy::GuardWithIf,
     TailStrategy::PredicateStores};
constexpr bool enable_sliding = true;
constexpr bool enable_hoisting = true;
constexpr bool enable_registers = true;
constexpr bool use_var_outermost = true;
constexpr bool partition_loops = true;
constexpr bool generate_upsamples = true;
constexpr bool generate_downsamples = true;
constexpr bool generate_flips = true;
// Resample by a symbolic factor rather than a literal 2, so that the number of
// warm-up iterations is an Expr rather than a constant. Mode 1 uses a Param
// directly, so its sign is unknown and the region required can't be shown to
// move monotonically; sliding should switch itself off rather than go wrong.
// Mode 2 clamps it positive, so sliding can still happen.
constexpr int symbolic_scale_modes = 3;
// How a Func's consumers index it. Like the pyramid level and the direction,
// this has to be a property of the Func rather than of each access, or it
// would have a footprint that grows with the loop var. NORMAL has to stay
// common, or hardly anything slides.
enum IndexMode {
    NORMAL,
    // A dimension indexed by a constant, so the region required of it in that
    // dimension doesn't depend on the loop vars at all.
    BROADCAST_X,
    BROADCAST_Y,
    // Both dimensions indexed by the same coordinate.
    DIAGONAL,
    // The dimensions swapped, so sliding over one loop slides the other
    // dimension.
    TRANSPOSE,
    // One dimension depending on both loop vars.
    SHEAR_X,
    SHEAR_Y,
    NUM_INDEX_MODES
};
constexpr bool generate_index_modes = true;
// Give some stages update definitions. A Func with updates slides differently:
// the bounds of every stage have to move together, and the region computed has
// to cover what the earlier stages wrote. An update that scatters along the
// dimension we'd slide stops us sliding at all.
constexpr bool generate_updates = true;
// Schedule the producers in ways that sliding window has to cope with:
// vectorizing or unrolling the dimension it slides, specializing the consumer
// so the loop nest appears twice, and prefetching, which carries a loop name
// that sliding has to keep up to date when it rewinds a loop.
// Splitting the dimension a Func slides along with a tail strategy that
// computes outside the region it was asked for corrupts the window: the values
// either side of the sliver this iteration computes belong to previous
// iterations and are still needed. That's true of RoundUp, ShiftInwards and
// both blend variants, so only GuardWithIf is used here.
constexpr bool generate_vectorize = true;
constexpr bool generate_unroll = true;
constexpr bool generate_specialize = true;
constexpr bool generate_prefetch = true;
// Bend some coordinates into monotonic but non-affine functions of themselves,
// which is what a stencil over something with a boundary condition looks like.
constexpr bool generate_piecewise_affine = true;
constexpr bool always_3x3_stencils = false;
constexpr bool always_1x3_stencils = false;
constexpr bool always_3x1_stencils = false;
constexpr bool static_bounds = false;
constexpr bool boundary_condition = true;
constexpr bool input_all_ones = false;
constexpr bool verbose = false;

Var x{"x"}, y{"y"}, yo{"yo"}, yi{"yi"};
Param<int> scale_param{"scale_param"};

// We build each pipeline twice, once with everything compute_root and once
// scheduled to slide, and check they agree. Both builds have to describe the
// same pipeline, so the first one records the choices it made and the second
// replays them.
class Rng {
    FuzzingContext &fuzz;
    std::vector<uint32_t> recorded;
    size_t pos = 0;
    bool replaying = false;

public:
    Rng(FuzzingContext &fuzz)
        : fuzz(fuzz) {
    }

    uint32_t operator()() {
        if (replaying && pos < recorded.size()) {
            return recorded[pos++];
        }
        // Past the end of the recording. The compute_root build makes no
        // scheduling decisions, so the sliding build draws more than it did;
        // those extra values only affect the schedule, which is what we want
        // to vary between the two.
        recorded.push_back(fuzz.ConsumeIntegral<uint32_t>());
        return recorded.back();
    }

    void replay() {
        replaying = true;
        pos = 0;
    }
};

// Bend a coordinate into a monotonically increasing but non-affine function of
// itself, so that the monotonicity analysis has to cope with something other
// than a linear ramp. The footprint stays bounded, so this doesn't stop
// anything sliding that otherwise would.
Expr make_piecewise_affine(const Expr &e, Rng &rng) {
    int kind = rng() % 4;
    int k = (int)(rng() % (size / 2));
    if (!generate_piecewise_affine) {
        return e;
    }
    switch (kind) {
    case 0:
        // Flat above a knee.
        return min(e, k);
    case 1:
        // Flat below a knee.
        return max(e, k - size / 2);
    case 2:
        // A step, so it's increasing but skips values.
        return select(e < k, e, e + 2);
    default:
        // Two different slopes.
        return select(e < k, e, (e * 2) - k);
    }
}

// Make a random stencil access to f from a consumer that is `rate` pyramid
// levels coarser than it (so -1 upsamples, 0 is a plain stencil, and 1
// downsamples). If flip is set, the consumer walks it backwards, which makes
// the region required move downwards as the loop advances.
// The factor by which one pyramid level resamples the one below it.
Expr resample_factor(int mode) {
    switch (mode) {
    case 1:
        // Sign unknown at compile time.
        return scale_param;
    case 2:
        // Known positive, but not a constant.
        return max(scale_param, 1);
    default:
        return 2;
    }
}

Expr random_use_of(Func f, Rng &rng, int rate, bool flip, int scale_mode,
                   IndexMode index_mode, bool *symbolic) {
    auto r = [&]() { return (int)(rng() % 5) - 2; };

    int x1 = r();
    int y1 = r();
    int x2 = r();
    int y2 = r();

    // Bend one coordinate some of the time. Draw the rng values
    // unconditionally so that turning this off doesn't change the rest of the
    // pipeline.
    bool bend_x = (rng() % 8) == 0;
    bool bend_y = (rng() % 8) == 0;
    Expr xc = make_piecewise_affine(x, rng);
    Expr yc = make_piecewise_affine(y, rng);
    if (!bend_x) {
        xc = x;
    }
    if (!bend_y) {
        yc = y;
    }
    *symbolic = *symbolic || bend_x || bend_y;

    if (flip && generate_flips) {
        xc = (size - 1) - xc;
        yc = (size - 1) - yc;
    }

    if (always_3x3_stencils) {
        x1 = y1 = 1;
        x2 = y2 = -1;
    }

    if (always_1x3_stencils) {
        x1 = 0;
        x2 = 0;
        y1 = 1;
        y2 = -1;
    }

    if (always_3x1_stencils) {
        x1 = 1;
        x2 = -1;
        y1 = 0;
        y2 = 0;
    }

    if (rate != 0 && scale_mode != 0) {
        *symbolic = true;
    }
    // Dividing by something that could be zero or negative is a different
    // problem to the one we're testing, so only the multiplies are symbolic.
    Expr up = max(resample_factor(scale_mode), 1);
    Expr down = resample_factor(scale_mode);
    if (rate < 0 && generate_upsamples) {
        xc = xc / up;
        yc = yc / up;
    } else if (rate > 0 && generate_downsamples) {
        xc = xc * down;
        yc = yc * down;
    }

    // A coordinate in the middle of the Func, for the broadcast modes.
    Expr k = size / 2;
    auto use = [&](int dx, int dy) {
        switch (generate_index_modes ? index_mode : NORMAL) {
        case BROADCAST_X:
            return f(k, yc + dy);
        case BROADCAST_Y:
            return f(xc + dx, k);
        case DIAGONAL:
            return f(xc + dx, xc + dy);
        case TRANSPOSE:
            return f(yc + dy, xc + dx);
        case SHEAR_X:
            return f(xc + yc + dx, yc + dy);
        case SHEAR_Y:
            return f(xc + dx, xc + yc + dy);
        default:
            return f(xc + dx, yc + dy);
        }
    };
    return use(x1, y1) + use(x2, y2);
}

// A location for compute_ats or store_ats.
struct Loop {
    // An index into our vector of stages
    int func;
    // A dim of the func, from outermost in. For the output we have
    // [outermost, yo, yi, x]. For everything else we have [outermost, y,
    // x].
    int var;

    bool operator==(const Loop &other) const {
        return func == other.func && var == other.var;
    }

    bool operator!=(const Loop &other) const {
        return !(*this == other);
    }

    bool is_root() const {
        return func < 0;
    }

    static Loop root() {
        return Loop{-1, -1};
    }
};

// A loop nest
using LoopNest = std::vector<Loop>;

std::ostream &operator<<(std::ostream &s, const LoopNest &l) {
    for (const auto &loop : l) {
        s << "(" << loop.func << ", " << loop.var << ")";
    }
    return s;
}

LoopNest common_prefix(const LoopNest &a, const LoopNest &b) {
    LoopNest l;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        if (a[i] == b[i]) {
            l.push_back(a[i]);
        } else {
            break;
        }
    }
    return l;
}

}  // namespace

FUZZ_TEST(sliding_window, FuzzingContext &fuzz) {
    // Filled once and reused, so that repeated runs don't spend all their time
    // in the boundary condition.
    static Buffer<uint8_t> input_buf(size, size);

    std::ostringstream source;

    Rng rng(fuzz);

    Buffer<uint8_t> correct_output, sliding_output;
    for (int sched = 0; sched < 2; sched++) {
        source = std::ostringstream{};

        if (sched == 1) {
            rng.replay();
        }

        ImageParam input(UInt(8), 2);
        source << "ImageParam input(UInt(8), 2);\n"
                  "Var x{\"x\"}, y{\"y\"}, yo{\"yo\"}, yi{\"yi\"};\n";

        struct Node {
            Func f;
            std::vector<Node *> used_by;
            std::vector<Var> vars;
            Loop hoist_storage, store_at, compute_at;
            LoopNest innermost;
            bool in_registers = false;
            // Which level of the pyramid this stage sits on, and whether it
            // is walked forwards or backwards relative to the output.
            int level = 0;
            bool flipped = false;
            // How this stage's consumers index it.
            IndexMode index_mode = NORMAL;
            // Whether this stage has an update definition, which makes the
            // region it computes bigger than the region required.
            bool has_update = false;
            // Whether any consumer indexes this stage in a way that stops its
            // footprint being a constant size: non-affinely, or by a symbolic
            // amount.
            bool bent = false;

            Node(const std::string &name)
                : f(name) {
            }
        };

        std::vector<Node> stages;
        for (int i = 0; i < num_stages; i++) {
            stages.emplace_back("f" + std::to_string(i));
        }

        source << "Func f[" << num_stages << "];\n";

        if (boundary_condition) {
            stages[0].f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
            source << "f[0](x, y) = BoundaryConditions::repeat_edge(input)(x, y);\n";
        } else {
            stages[0].f(x, y) = input(x, y);
            source << "f[0](x, y) = input(x, y);\n";
        }

        // Lay the stages out on the levels of a pyramid. A stage may only
        // consume stages on its own level or one step away, and the sampling
        // rate of each access is fixed by the difference in levels. Real
        // pipelines look like this, and it means the footprint of a Func never
        // grows with the loop var, which would otherwise make its storage
        // impossible to bound.
        // One factor per pipeline, so that every level agrees on it.
        int scale_mode = rng() % symbolic_scale_modes;

        std::vector<int> level(num_stages, 0);
        std::vector<bool> flipped(num_stages, false);
        std::vector<IndexMode> index_mode(num_stages, NORMAL);
        for (int i = 0; i < num_stages; i++) {
            // Weighted towards NORMAL, so that pipelines still mostly slide.
            index_mode[i] = ((rng() % 3) == 0) ?
                                (IndexMode)(rng() % NUM_INDEX_MODES) :
                                NORMAL;
        }
        for (int i = 1; i < num_stages; i++) {
            int parent = rng() % i;
            level[i] = level[parent] + (int)(rng() % 3) - 1;
            // Like the level, the direction each stage is walked in has to be
            // a property of the stage, or a Func read both forwards and
            // backwards would have a footprint that grows with the loop var.
            flipped[i] = flipped[parent] != ((rng() % 4) == 0);
        }

        for (int i = 0; i < num_stages; i++) {
            stages[i].level = level[i];
            stages[i].flipped = flipped[i];
            stages[i].index_mode = index_mode[i];
        }

        for (int i = 1; i < num_stages; i++) {
            std::vector<int> candidates;
            for (int j = 0; j < i; j++) {
                if (std::abs(level[i] - level[j]) <= 1) {
                    candidates.push_back(j);
                }
            }
            int i1 = candidates[rng() % candidates.size()];
            int i2 = candidates[rng() % candidates.size()];
            Node *in_1 = &stages[i1];
            Node *in_2 = &stages[i2];

            Expr rhs =
                (random_use_of(in_1->f, rng, level[i] - level[i1],
                               flipped[i] != flipped[i1], scale_mode,
                               index_mode[i1], &in_1->bent) +
                 random_use_of(in_2->f, rng, level[i] - level[i2],
                               flipped[i] != flipped[i2], scale_mode,
                               index_mode[i2], &in_2->bent));

            stages[i].f(x, y) = rhs;

            stages[i1].used_by.push_back(&stages[i]);
            stages[i2].used_by.push_back(&stages[i]);

            if (i == num_stages - 1) {
                stages[i].vars.push_back(Var::outermost());
                stages[i].vars.push_back(yo);
                stages[i].vars.push_back(yi);
                stages[i].vars.push_back(x);
            } else {
                stages[i].vars.push_back(Var::outermost());
                stages[i].vars.push_back(y);
                stages[i].vars.push_back(x);
            }

            if (i == num_stages - 1) {
                stages[i].innermost.push_back(Loop::root());
            }
            for (int j = 0; j < (i == num_stages - 1 ? 4 : 3); j++) {
                stages[i].innermost.push_back(Loop{i, j});
            }

            // Maybe give this stage an update definition.
            int update_kind = rng() % 8;
            if (generate_updates && i < num_stages - 1) {
                switch (update_kind) {
                case 0:
                    // Pure in both dimensions, so it can still slide.
                    stages[i].f(x, y) += cast<uint8_t>(1);
                    source << "f[" << i << "](x, y) += cast<uint8_t>(1);\n";
                    break;
                case 1:
                    // Pure, and reads itself.
                    stages[i].f(x, y) = stages[i].f(x, y) * cast<uint8_t>(3);
                    source << "f[" << i << "](x, y) = f[" << i << "](x, y)*3;\n";
                    break;
                case 2: {
                    // Scatters along x, so we mustn't slide along x.
                    RDom r(0, 3);
                    stages[i].f(r, y) += cast<uint8_t>(2);
                    source << "RDom r" << i << "(0, 3);\n"
                           << "f[" << i << "](r" << i << ", y) += cast<uint8_t>(2);\n";
                    break;
                }
                case 3: {
                    // Scatters along y, so we mustn't slide along y.
                    RDom r(0, 3);
                    stages[i].f(x, r) += cast<uint8_t>(2);
                    source << "RDom r" << i << "(0, 3);\n"
                           << "f[" << i << "](x, r" << i << ") += cast<uint8_t>(2);\n";
                    break;
                }
                default:
                    break;
                }
                if (update_kind < 4) {
                    stages[i].has_update = true;
                    // We only ever schedule the last stage of a Func, which
                    // Halide warns about unless we say it's deliberate.
                    stages[i].f.update(0).unscheduled();
                    source << "f[" << i << "].update(0).unscheduled();\n";
                }
            }

            std::ostringstream rhs_source;
            rhs_source << Internal::simplify(rhs);

            // Fix up the source code for the calls
            std::string rhs_str = rhs_source.str();
            rhs_str = Internal::replace_all(rhs_str, "(uint8)", "");
            rhs_str = Internal::replace_all(rhs_str, in_1->f.name(), "f[" + std::to_string(i1) + "]");
            rhs_str = Internal::replace_all(rhs_str, in_2->f.name(), "f[" + std::to_string(i2) + "]");
            source << "f[" << i << "](x, y) = " << rhs_str << ";\n";
        }

        std::set<const Node *> live_funcs;
        live_funcs.insert(&stages.back());
        for (int i = num_stages - 1; i >= 0; i--) {
            for (const Node *consumer : stages[i].used_by) {
                if (live_funcs.count(consumer)) {
                    live_funcs.insert(&stages[i]);
                }
            }
        }

        if (sched == 0) {
            // compute_root everything to get a reference output
            for (int i = 0; i < num_stages; i++) {
                stages[i].f.compute_root();
            }
        } else {
            // Give it a random legal schedule that uses sliding window
            constexpr int num_tail_strategies = sizeof(output_tail_strategies) / sizeof(output_tail_strategies[0]);
            auto strat = output_tail_strategies[rng() % num_tail_strategies];
            for (auto producer = stages.rbegin() + 1; producer != stages.rend(); producer++) {
                if (!live_funcs.count(&(*producer))) {
                    continue;
                }

                // Compute the common prefix of all consumers
                LoopNest loc;
                for (auto consumer = producer->used_by.begin();
                     consumer != producer->used_by.end(); consumer++) {
                    if (live_funcs.count(*consumer)) {
                        if (loc.empty()) {
                            loc = (*consumer)->innermost;
                        } else {
                            loc = common_prefix(loc, (*consumer)->innermost);
                        }
                    }
                }
                assert(!loc.empty());

                // A PredicateStores split still runs the producers for the
                // predicated-off tail iteration of the inner loop, so anything
                // computed in there accesses outside the region its allocation
                // was sized for. Only offer sites at or outside the outer loop.
                // See https://github.com/halide/Halide/issues/9322
                if (strat == TailStrategy::PredicateStores) {
                    while (loc.size() > 1 &&
                           loc.back().func == num_stages - 1 &&
                           loc.back().var >= 2) {
                        loc.pop_back();
                    }
                }

                // Drop some levels at random to get legal store_at and compute_at sites
                std::vector<int> levels;
                for (int i = 0; i < 3; i++) {
                    levels.push_back(rng() % (int)loc.size());
                    if (!use_var_outermost) {
                        while (levels.back() > 0 && loc[levels.back()].var == 0) {
                            levels.back()--;
                        }
                    }
                }
                std::sort(levels.begin(), levels.end());
                producer->hoist_storage = loc[levels[0]];
                producer->store_at = loc[levels[1]];
                producer->compute_at = loc[levels[2]];

                // Slide in registers sometimes. Draw the rng value
                // unconditionally, so that turning this off doesn't change the
                // rest of the schedule.
                bool want_registers = (rng() % 4) != 0;

                // A register allocation must have constant extent in every
                // dimension. That needs the func to be computed in the
                // innermost loop of its consumers (so the unslid dimensions
                // only span one iteration's footprint), and read only by the
                // output stage, at the output's own level of the pyramid (so
                // that the footprint is a handful of taps that advance one per
                // iteration, rather than something that grows with the loop
                // var). That's the line-buffer shape that register sliding is
                // for. Halide has the last word on whether an allocation can
                // live in registers, and being told no is a compile error we
                // can't recover from, so we have to stay well inside what it
                // will accept.
                const Loop &c = producer->compute_at;
                const Node *first_consumer = nullptr;
                bool one_consumer_site = true;
                for (const Node *consumer : producer->used_by) {
                    if (!live_funcs.count(consumer)) {
                        continue;
                    }
                    if (!first_consumer) {
                        first_consumer = consumer;
                    }
                    one_consumer_site = one_consumer_site &&
                                        consumer->level == first_consumer->level &&
                                        consumer->compute_at == first_consumer->compute_at;
                }
                bool small_enough_for_registers =
                    !c.is_root() &&
                    c.var == (int)stages[c.func].vars.size() - 1 &&
                    !producer->bent &&
                    one_consumer_site &&
                    first_consumer == &stages.back() &&
                    first_consumer->level == producer->level &&
                    first_consumer->flipped == producer->flipped &&
                    producer->index_mode == NORMAL &&
                    !producer->has_update;

                producer->in_registers =
                    want_registers && enable_registers && small_enough_for_registers;

                if (!enable_sliding) {
                    producer->store_at = producer->compute_at;
                    producer->in_registers = false;
                }
                if (!enable_hoisting) {
                    producer->hoist_storage = producer->store_at;
                }

                // Rewrite innermost to include containing loops
                producer->innermost.insert(producer->innermost.begin(), loc.begin(), loc.begin() + levels[2] + 1);
            }

            Func output_func = stages.back().f;
            std::string output_func_str = "f[" + std::to_string(num_stages - 1) + "]";
            source << output_func_str;

            if (!partition_loops) {
                output_func.never_partition_all();
                source << ".never_partition_all()";
            }

            output_func.split(y, yo, yi, split_factor, strat);
            bool want_specialize = (rng() % 4) == 0;
            if (generate_specialize && want_specialize) {
                // Two copies of the loop nest, which sliding window has to
                // treat independently.
                output_func.specialize(output_func.output_buffer().dim(0).min() == 0);
                source << output_func_str
                       << ".specialize(output_func.output_buffer().dim(0).min() == 0);\n";
            }
            source << ".split(y, yo, yi, "
                   << split_factor << ", TailStrategy::" << strat << ");\n";

            if (static_bounds) {
                output_func.output_buffer().dim(0).set_bounds(0, size);
                output_func.output_buffer().dim(1).set_bounds(0, size);
                source << "output_func.output_buffer().dim(0).set_bounds(0, " << size << ");\n"
                       << "output_func.output_buffer().dim(1).set_bounds(0, " << size << ");\n";
            }

            for (int i = 0; i < num_stages - 1; i++) {
                if (!live_funcs.count(&stages[i])) {
                    continue;
                }
                std::string func_str = "f[" + std::to_string(i) + "]";
                source << func_str;
                Loop hoist_storage = stages[i].hoist_storage;
                Loop store_at = stages[i].store_at;
                Loop compute_at = stages[i].compute_at;

                if (!partition_loops) {
                    // Loop partitioning happens after sliding window and
                    // storage folding, and makes the IR harder to read.
                    source << ".never_partition_all()";
                    stages[i].f.never_partition_all();
                }

                auto var_name = [](const Var &v) {
                    if (v.name() == Var::outermost().name()) {
                        return std::string{"Var::outermost()"};
                    } else {
                        return v.name();
                    }
                };

                if (hoist_storage != store_at) {
                    if (hoist_storage.is_root()) {
                        stages[i].f.hoist_storage_root();
                        source << ".hoist_storage_root()";
                    } else {
                        Func f = stages[hoist_storage.func].f;
                        Var v = stages[hoist_storage.func].vars[hoist_storage.var];
                        stages[i].f.hoist_storage(f, v);
                        source << ".hoist_storage(f[" << hoist_storage.func << "], " << var_name(v) << ")";
                    }
                }
                if (store_at != compute_at) {
                    if (store_at.is_root()) {
                        stages[i].f.store_root();
                        source << ".store_root()";
                    } else {
                        Func f = stages[store_at.func].f;
                        Var v = stages[store_at.func].vars[store_at.var];
                        stages[i].f.store_at(f, v);
                        source << ".store_at(f[" << store_at.func << "], " << var_name(v) << ")";
                    }
                }
                {
                    if (compute_at.is_root()) {
                        stages[i].f.compute_root();
                        source << ".compute_root()";
                    } else {
                        Func f = stages[compute_at.func].f;
                        Var v = stages[compute_at.func].vars[compute_at.var];
                        stages[i].f.compute_at(f, v);
                        source << ".compute_at(f[" << compute_at.func << "], " << var_name(v) << ")";
                    }
                }

                // Vectorizing or unrolling the producer's own loops changes
                // the region it provides, which sliding has to account for.
                // Draw the rng values unconditionally so that turning these
                // off doesn't change the rest of the schedule.
                int inner_sched = rng() % 6;
                if (inner_sched < 2) {
                    // Splitting the dimension we'd slide stops it sliding, so
                    // there's no small window to keep in registers.
                    stages[i].in_registers = false;
                }
                if (inner_sched == 0 && generate_vectorize) {
                    stages[i].f.vectorize(x, 4, TailStrategy::GuardWithIf);
                    source << ".vectorize(x, 4, TailStrategy::GuardWithIf)";
                } else if (inner_sched == 1 && generate_unroll) {
                    stages[i].f.unroll(y, 2, TailStrategy::GuardWithIf);
                    source << ".unroll(y, 2, TailStrategy::GuardWithIf)";
                }

                if ((rng() % 6) == 0 && generate_prefetch && !compute_at.is_root()) {
                    Var v = stages[compute_at.func].vars[compute_at.var];
                    stages[i].f.prefetch(stages[i].f, v, v, 1);
                    source << ".prefetch(f[" << i << "], " << var_name(v) << ", "
                           << var_name(v) << ", 1)";
                }
                if (stages[i].in_registers) {
                    stages[i].f.store_in(MemoryType::Register);
                    source << ".store_in(MemoryType::Register)";
                }
                source << ";\n";
            }
            if (verbose) {
                std::cout << source.str() << "\n";
            }
        }

        scale_param.set(2);

        if (boundary_condition) {
            input.set(input_buf);
        } else {
            input.reset();
            stages.back().f.infer_input_bounds({size, size});
        }

        static bool first_run = true;
        std::mt19937 input_fill_rng{(uint32_t)rng()};
        if (!boundary_condition || first_run) {
            if (input_all_ones) {
                input.get().as<uint8_t>().fill(1);
            } else {
                input.get().as<uint8_t>().fill(input_fill_rng);
            }
        }
        first_run = false;

        if (sched == 0) {
            correct_output = stages.back().f.realize({size, size});
        } else {
            sliding_output = stages.back().f.realize({size, size});
        }
    }

    bool ok = true;
    for (int y = 0; y < correct_output.height(); y++) {
        for (int x = 0; x < correct_output.width(); x++) {
            if (ok && correct_output(x, y) != sliding_output(x, y)) {
                std::cout
                    << "correct_output(" << x << ", " << y << ") = "
                    << (int)correct_output(x, y) << "\n"
                    << "sliding_output(" << x << ", " << y << ") = "
                    << (int)sliding_output(x, y) << "\n";
                ok = false;
            }
        }
    }
    if (!ok) {
        std::cout << source.str() << "\n";
        return 1;
    }
    return 0;
}
