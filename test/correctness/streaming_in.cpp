#include <Halide.h>
using namespace Halide;
using namespace Halide::Internal;

// stream_loads() is a Stage property: a Stage requests streaming for its own
// direct reads of named Funcs (or, with no arguments, every direct read
// except a self-load). The request is resolved against the Stage's fully
// inlined body, so it correctly reaches through inlined intermediates -- but
// it can only ever affect a Stage that survives lowering as a real,
// materialized read. This test verifies the design:
//
//  1. h.stream_loads({f}) works even though h only directly calls an
//     inlined intermediate g (which in turn calls f): once g inlines into
//     h, h's flattened body directly loads f, and that's the load that gets
//     marked.
//  2. For the residual-network case that motivated this feature --
//     h(x) = f(x) + g(x), g(x) = ...f(x)..., wanting f streamed for h but
//     not for g -- the correct, zero-overhead idiom is h.stream_loads({f}).
//     g's independent read of f is left untouched, since stream_loads() only
//     ever affects the one Stage that requested it.
//  3. If f.in(h).stream_loads() is used without specifying a compute location,
//     it's diagnosed with a user_warning rather than silently dropped.

namespace {

struct Finder : public IRVisitor {
    using IRVisitor::visit;

    std::string current_producer;
    // (producer name, target name) -> (streaming count, ordinary count)
    std::map<std::pair<std::string, std::string>, std::pair<int, int>> loads;

protected:
    void visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            ScopedValue old(current_producer, op->name);
            IRVisitor::visit(op);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
        auto &p = loads[{current_producer, op->name}];
        (op->is_streaming ? p.first : p.second)++;
        IRVisitor::visit(op);
    }
};

int streaming_count(Finder &finder, const std::string &producer, const std::string &target) {
    auto it = finder.loads.find({producer, target});
    return it == finder.loads.end() ? 0 : it->second.first;
}

int ordinary_count(Finder &finder, const std::string &producer, const std::string &target) {
    auto it = finder.loads.find({producer, target});
    return it == finder.loads.end() ? 0 : it->second.second;
}

struct WarningCounter final : CompileTimeErrorReporter {
    int warnings_occurred = 0;

    void warning(const char *msg) override {
        warnings_occurred++;
    }

    [[noreturn]] void error(const char *msg) override {
        std::fprintf(stderr, "Unexpected error: %s\n", msg);
        exit(1);
    }
};

}  // namespace

int main(int argc, char **argv) {
    // Case 1: h only directly calls the inlined intermediate g, which
    // directly calls f. h.stream_loads({f}) still works: it's resolved
    // against h's fully-inlined body, where the load of f is now direct.
    {
        Func f("streaming_in_case1_f"), g("streaming_in_case1_g"), h("streaming_in_case1_h");
        Var x;
        f(x) = cast<float>(x) * 3.0f + 1.0f;
        g(x) = f(x);  // left at the default (inline) schedule
        h(x) = g(x) + 1.0f;

        f.compute_root();
        h.compute_root();
        h.stream_loads({f});

        Finder finder;
        Module m = h.compile_to_module({}, h.name());
        for (const auto &fn : m.functions()) {
            fn.body.accept(&finder);
        }
        if (streaming_count(finder, h.name(), f.name()) != 1 ||
            ordinary_count(finder, h.name(), f.name()) != 0) {
            std::fprintf(stderr, "Case 1: expected h.stream_loads({f}) to mark h's "
                                 "(post-inline) direct load of f, found streaming=%d "
                                 "ordinary=%d\n",
                         streaming_count(finder, h.name(), f.name()),
                         ordinary_count(finder, h.name(), f.name()));
            return 1;
        }
    }

    // Case 2: the residual-network case, solved directly. h(x) = f(x) +
    // g(x), g(x) also depends on f(x). We want f's data streamed when read
    // for h, but not when read by g -- with no extra Func and no extra
    // buffer: just h.stream_loads({f}).
    {
        Func f("streaming_in_case2_f"), g("streaming_in_case2_g"), h("streaming_in_case2_h");
        Var x;
        f(x) = cast<float>(x) * 3.0f + 1.0f;
        g(x) = f(x) * 2.0f;
        h(x) = f(x) + g(x);

        f.compute_root();
        g.compute_root();
        h.compute_root();
        h.stream_loads({f});

        Finder finder;
        Module m = h.compile_to_module({}, h.name());
        for (const auto &fn : m.functions()) {
            fn.body.accept(&finder);
        }
        if (streaming_count(finder, h.name(), f.name()) != 1 ||
            ordinary_count(finder, h.name(), f.name()) != 0) {
            std::fprintf(stderr, "Case 2: expected h's own read of f to be streaming, "
                                 "found streaming=%d ordinary=%d\n",
                         streaming_count(finder, h.name(), f.name()),
                         ordinary_count(finder, h.name(), f.name()));
            return 1;
        }
        if (streaming_count(finder, g.name(), f.name()) != 0 ||
            ordinary_count(finder, g.name(), f.name()) != 1) {
            std::fprintf(stderr, "Case 2: expected g's independent read of f to remain "
                                 "ordinary, found streaming=%d ordinary=%d\n",
                         streaming_count(finder, g.name(), f.name()),
                         ordinary_count(finder, g.name(), f.name()));
            return 1;
        }

        // Correctness, not just IR shape.
        Buffer<float> result = h.realize({16});
        for (int i = 0; i < 16; i++) {
            float f_val = i * 3.0f + 1.0f;
            float expected = f_val + f_val * 2.0f;
            if (result(i) != expected) {
                std::fprintf(stderr, "Case 2: incorrect result at %d: %f (expected %f)\n",
                             i, result(i), expected);
                return 1;
            }
        }
    }

    // Case 3: an f.in(h) wrapper, left at its default (inlined) schedule.
    // stream_loads() is a complete no-op -- with no materialized Stage of
    // its own, there's no distinct read left to mark -- and it's diagnosed
    // with a user_warning, matching vectorize/parallel/unroll/split/bound
    // on an inlined Func (see validate_schedule_inlined_function).
    {
        Func f("streaming_in_case3_f"), g("streaming_in_case3_g"), h("streaming_in_case3_h");
        Var x;
        f(x) = cast<float>(x) * 3.0f + 1.0f;
        g(x) = f(x) * 2.0f;
        h(x) = f(x) + g(x);

        Func f_in_h = f.in(h);
        f_in_h.stream_loads({f});  // no compute_at/compute_root: stays inlined

        f.compute_root();
        g.compute_root();
        h.compute_root();

        WarningCounter reporter;
        set_custom_compile_time_error_reporter(&reporter);
        Module m = h.compile_to_module({}, h.name());
        set_custom_compile_time_error_reporter(nullptr);
        if (reporter.warnings_occurred == 0) {
            std::fprintf(stderr, "Case 3: expected a user_warning when stream_loads() is "
                                 "requested on a Func that ends up inlined, found none\n");
            return 1;
        }

        int streaming_loads_found = 0;
        for (const auto &fn : m.functions()) {
            visit_with(fn.body, [&](auto *self, const auto *op) {
                if constexpr (std::is_same_v<decltype(op), const StreamingLoads *>) {
                    ++streaming_loads_found;
                } else if constexpr (std::is_same_v<decltype(op), const Load *>) {
                    if (op->name.rfind(f.name(), 0) == 0 && op->is_streaming) {
                        ++streaming_loads_found;
                    }
                }
                self->visit_base(op);
            });
        }
        if (streaming_loads_found != 0) {
            std::fprintf(stderr, "Case 3: expected stream_loads() on an inlined "
                                 "Func::in() wrapper to be a complete no-op, found %d\n",
                         streaming_loads_found);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
