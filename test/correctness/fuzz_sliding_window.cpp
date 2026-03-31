#include "Halide.h"

using namespace Halide;

// Configuration settings. If you find a failure, you can progressively simplify
// the IR by turning things on and off.

constexpr int num_trials = 100;  // Use -1 for infinite
constexpr bool stop_on_first_failure = true;

// We want large pipelines to get into complex situations, but small
// pipelines so that we can test lots of them and so that the
// failures are understandable by humans.
constexpr int num_stages = 5;

// None of these configuration options should change the number of calls to the
// rng, or else you can't progressively simplify a repro.
constexpr int size = 15;
constexpr int split_factor = 4;
constexpr TailStrategy output_tail_strategies[] =
    {TailStrategy::ShiftInwards,
     TailStrategy::GuardWithIf,
     TailStrategy::RoundUp};
constexpr bool enable_sliding = true;
constexpr bool enable_hoisting = false;  // Turned off due to https://github.com/halide/Halide/issues/8141
constexpr bool use_var_outermost = true;
constexpr bool partition_loops = true;
constexpr bool generate_upsamples = true;
constexpr bool generate_downsamples = true;
constexpr bool always_3x3_stencils = false;
constexpr bool always_1x3_stencils = false;
constexpr bool always_3x1_stencils = false;
constexpr bool static_bounds = false;
constexpr bool boundary_condition = true;
constexpr bool input_all_ones = false;
constexpr bool verbose = false;

Var x{"x"}, y{"y"}, yo{"yo"}, yi{"yi"};

Expr random_use_of(Func f, std::mt19937 &rng) {
    auto r = [&]() { return (int)(rng() % 5) - 2; };

    int x1 = r();
    int y1 = r();
    int x2 = r();
    int y2 = r();

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

    int type = rng() % 3;

    if (type == 1 && generate_upsamples) {
        return f(x / 2 + x1, y / 2 + y1) + f(x / 2 + x2, y / 2 + y2);
    } else if (type == 2 && generate_downsamples) {
        return f(x * 2 + x1, y * 2 + y1) + f(x * 2 + x2, y * 2 + y2);
    } else {
        return f(x + x1, y + y1) + f(x + x2, y + y2);
    }
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

bool run_trial(int trial, uint32_t seed, const Buffer<uint8_t> &input_buf) {

    if (verbose) {
        std::cout << "Trial " << trial << " with seed " << seed << "\n";
    }

    std::ostringstream source;

    std::mt19937 rng;

    Buffer<uint8_t> correct_output, sliding_output;
    for (int sched = 0; sched < 2; sched++) {
        source = std::ostringstream{};

        rng.seed(seed);

        ImageParam input(UInt(8), 2);
        source << "ImageParam input(UInt(8), 2);\n"
                  "Var x{\"x\"}, y{\"y\"}, yo{\"yo\"}, yi{\"yi\"};\n";

        struct Node {
            Func f;
            std::vector<Node *> used_by;
            std::vector<Var> vars;
            Loop hoist_storage, store_at, compute_at;
            LoopNest innermost;

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

        for (int i = 1; i < num_stages; i++) {
            int i1 = rng() % i;
            int i2 = rng() % i;
            Node *in_1 = &stages[i1];
            Node *in_2 = &stages[i2];

            Expr rhs =
                (random_use_of(in_1->f, rng) +
                 random_use_of(in_2->f, rng));

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

                if (!enable_sliding) {
                    producer->store_at = producer->compute_at;
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

            constexpr int num_tail_strategies = sizeof(output_tail_strategies) / sizeof(output_tail_strategies[0]);
            auto strat = output_tail_strategies[rng() % num_tail_strategies];
            output_func.split(y, yo, yi, split_factor, strat);
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
                source << ";\n";
            }
            if (verbose) {
                std::cout << source.str() << "\n";
            }
        }

        if (boundary_condition) {
            input.set(input_buf);
        } else {
            input.reset();
            stages.back().f.infer_input_bounds({size, size});
        }

        static bool first_run = true;
        std::mt19937 input_fill_rng{rng()};
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
        std::cout << "Failed on trial " << trial << " with seed " << seed << "\n"
                  << source.str() << "\n";
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    uint32_t initial_seed = time(NULL);

    std::mt19937 trial_seed_generator{(uint32_t)initial_seed};

    Buffer<uint8_t> input_buf(size, size);

    bool repro_mode = argc == 2;
    uint32_t repro_seed = repro_mode ? (uint32_t)std::atol(argv[1]) : 0;

    int num_failures = 0;

    if (repro_mode) {
        num_failures = run_trial(0, repro_seed, input_buf) ? 0 : 1;
    } else {
        std::cout << "Initial seed = " << initial_seed << "\n";
        for (int trial = 0; trial != num_trials - 1; trial++) {
            num_failures += run_trial(trial, trial_seed_generator(), input_buf) ? 0 : 1;
            if (num_failures > 0 && stop_on_first_failure) {
                break;
            }
        }
    }

    if (num_failures > 0) {
        std::cout << num_failures << " failures\n";
        return 1;
    } else {
        std::cout << "Success!\n";
        return 0;
    }
}
