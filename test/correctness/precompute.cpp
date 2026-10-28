#include "Halide.h"
#include "halide_test_dirs.h"

#include <cmath>
#include <stdio.h>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// The Buffers a precompute() directive embedded in a Module.
std::vector<Buffer<>> precomputed_buffers(const Module &m) {
    std::vector<Buffer<>> result;
    for (const Buffer<> &b : m.buffers()) {
        if (b.name().find("_precomputed") != std::string::npos) {
            result.push_back(b);
        }
    }
    return result;
}

std::vector<Buffer<>> precomputed_buffers(Func f, const Target &t = get_jit_target_from_environment()) {
    return precomputed_buffers(f.compile_to_module({}, "f", t));
}

// Checks that a Func is never realized in the lowered code.
class CheckNotRealized : public Internal::IRMutator {
    using IRMutator::visit;

    Internal::Stmt visit(const Internal::ProducerConsumer *op) override {
        realized = realized || op->name == name;
        return IRMutator::visit(op);
    }

    Internal::Stmt visit(const Internal::Allocate *op) override {
        realized = realized || op->name == name;
        return IRMutator::visit(op);
    }

public:
    std::string name;
    bool realized = false;

    explicit CheckNotRealized(const std::string &name)
        : name(name) {
    }
};

// Records compile-time warnings, so tests can check which were issued.
class WarningRecorder : public CompileTimeErrorReporter {
public:
    std::vector<std::string> warnings;

    void warning(const char *msg) override {
        warnings.emplace_back(msg);
    }

    [[noreturn]] void error(const char *msg) override {
        printf("Unexpected error: %s\n", msg);
        exit(1);
    }
};

int count_precompute_warnings(Func f, const Target &t) {
    WarningRecorder recorder;
    set_custom_compile_time_error_reporter(&recorder);
    f.compile_to_module({}, "f", t);
    set_custom_compile_time_error_reporter(nullptr);
    int count = 0;
    for (const std::string &w : recorder.warnings) {
        if (w.find("precompute()") != std::string::npos) {
            count++;
        }
    }
    return count;
}

// A Target that isn't the host, so compilation for it is cross-compilation.
Target cross_target() {
    Target host = get_host_target();
    if (host.arch == Target::X86) {
        return Target("arm-64-linux");
    }
    return Target("x86-64-linux");
}

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: Check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

template<typename T>
int check_equal(const Buffer<T> &actual, const Buffer<T> &expected) {
    for (int i = 0; i < expected.width(); i++) {
        if (actual(i) != expected(i)) {
            printf("Mismatch at %d: %f vs %f\n", i, (double)actual(i), (double)expected(i));
            return 1;
        }
    }
    return 0;
}

// A lookup table gives the same result whether it's inlined or precomputed.
int integer_lut() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = cast<uint8_t>((x * x * 7 + 3) % 251);
    g(x) = lut(x % 256) + lut((x * 3) % 256);

    Buffer<uint8_t> expected = g.realize({1000});
    lut.precompute();
    g.vectorize(x, 8);
    Buffer<uint8_t> actual = g.realize({1000});
    CHECK(check_equal(actual, expected) == 0);

    CheckNotRealized checker(lut.name());
    g.add_custom_lowering_pass(&checker, []() {});
    g.compile_jit();
    CHECK(!checker.realized);
    g.clear_custom_lowering_passes();

    std::vector<Buffer<>> buffers = precomputed_buffers(g);
    CHECK(buffers.size() == 1);
    CHECK(buffers[0].type() == UInt(8));
    CHECK(buffers[0].dimensions() == 1);
    CHECK(buffers[0].dim(0).min() == 0);
    CHECK(buffers[0].dim(0).extent() == 256);
    return 0;
}

int float_lut() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = sin(x * 0.1f) + sqrt(cast<float>(x));
    g(x) = lut(x % 64);

    Buffer<float> expected = g.realize({200});
    lut.precompute();
    Buffer<float> actual = g.realize({200});
    for (int i = 0; i < 200; i++) {
        CHECK(std::abs(actual(i) - expected(i)) < 1e-5f);
    }
    return 0;
}

// A precomputed Func can call other constant Funcs, whatever their schedule,
// and read embedded Buffers.
int calls_constant_funcs() {
    Var x("x"), y("y");
    Buffer<int> table(8);
    for (int i = 0; i < 8; i++) {
        table(i) = i * i - 3;
    }

    Func a("a"), b("b"), lut("lut"), g("g");
    a(x) = x * 3;
    b(x, y) = a(x) + a(y) * table(y % 8);
    lut(x, y) = b(x, y) + b(y, x);
    g(x, y) = lut(x % 10, y % 6);

    Buffer<int> expected = g.realize({30, 20});
    a.compute_root().vectorize(x, 4);
    b.compute_at(lut, y);
    lut.precompute();
    Buffer<int> actual = g.realize({30, 20});
    for (int j = 0; j < 20; j++) {
        for (int i = 0; i < 30; i++) {
            CHECK(actual(i, j) == expected(i, j));
        }
    }

    std::vector<Buffer<>> buffers = precomputed_buffers(g);
    CHECK(buffers.size() == 1);
    CHECK(buffers[0].dim(0).extent() == 10);
    CHECK(buffers[0].dim(1).extent() == 6);
    return 0;
}

// Update definitions and constant RDoms are fine.
int update_and_rdom() {
    Var x("x");
    RDom r(0, 100, "r");
    Func hist("hist"), sum("sum"), g("g");
    hist(x) = 0;
    hist((r * r) % 16) += 1;
    sum(x) = 0;
    sum(x) += x * r;
    g(x) = hist(x % 16) + sum(x % 5);

    Buffer<int> expected = g.realize({100});
    hist.precompute();
    sum.precompute();
    sum.update().vectorize(x, 4);
    Buffer<int> actual = g.realize({100});
    CHECK(check_equal(actual, expected) == 0);
    return 0;
}

// A precomputed Func can call another precomputed Func. Only the one that's
// read by the pipeline gets embedded.
int precomputed_calls_precomputed() {
    Var x("x");
    Func p1("p1"), p2("p2"), g("g");
    p1(x) = x * x;
    p2(x) = p1(x) + p1(x + 1);
    g(x) = p2(x % 10);

    Buffer<int> expected = g.realize({50});
    p1.precompute();
    p2.precompute();
    Buffer<int> actual = g.realize({50});
    CHECK(check_equal(actual, expected) == 0);

    std::vector<Buffer<>> buffers = precomputed_buffers(g);
    CHECK(buffers.size() == 1);
    CHECK(buffers[0].dim(0).extent() == 10);
    return 0;
}

// A wrapper of a precomputed Func reads the embedded Buffer, and can be
// computed anywhere.
int wrapper() {
    Var x("x"), xo("xo"), xi("xi");
    Func lut("lut"), g("g");
    lut(x) = x * 5 - 2;
    g(x) = lut(x % 32) * 2;

    Buffer<int> expected = g.realize({256});
    lut.precompute();
    g.split(x, xo, xi, 16);
    lut.in(g).compute_at(g, xo).vectorize(x, 4);
    Buffer<int> actual = g.realize({256});
    CHECK(check_equal(actual, expected) == 0);
    CHECK(precomputed_buffers(g).size() == 1);
    return 0;
}

// The region precomputed is the region allocated, including any padding.
int padding() {
    Var x("x");
    {
        // A consumer that rounds up its loop reads past the end of the region
        // the algorithm needs.
        Func lut("lut"), c("c"), g("g");
        lut(x) = x * 3 + 1;
        c(x) = lut(x) + 1;
        g(x) = c(x % 13);
        lut.precompute();
        c.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
        Buffer<int> actual = g.realize({40});
        for (int i = 0; i < 40; i++) {
            CHECK(actual(i) == (i % 13) * 3 + 2);
        }

        std::vector<Buffer<>> buffers = precomputed_buffers(g);
        CHECK(buffers.size() == 1);
        Buffer<int> b = buffers[0];
        CHECK(b.dim(0).min() == 0);
        CHECK(b.dim(0).extent() == 16);
        for (int i = 0; i < 16; i++) {
            CHECK(b(i) == i * 3 + 1);
        }
    }
    {
        // align_bounds pads the region of the precomputed Func itself.
        Func lut("lut"), g("g");
        lut(x) = x * 2;
        g(x) = lut(x % 13 + 3);
        lut.precompute().align_bounds(x, 8);
        Buffer<int> actual = g.realize({40});
        for (int i = 0; i < 40; i++) {
            CHECK(actual(i) == (i % 13 + 3) * 2);
        }

        std::vector<Buffer<>> buffers = precomputed_buffers(g);
        CHECK(buffers.size() == 1);
        Buffer<int> b = buffers[0];
        CHECK(b.dim(0).min() == 0);
        CHECK(b.dim(0).extent() == 16);
        for (int i = 0; i < 16; i++) {
            CHECK(b(i) == i * 2);
        }
    }
    {
        // A region that doesn't start at zero.
        Func lut("lut"), g("g");
        lut(x) = x * x;
        g(x) = lut(x % 10 - 5);
        lut.precompute();
        Buffer<int> actual = g.realize({40});
        for (int i = 0; i < 40; i++) {
            CHECK(actual(i) == (i % 10 - 5) * (i % 10 - 5));
        }

        std::vector<Buffer<>> buffers = precomputed_buffers(g);
        CHECK(buffers.size() == 1);
        CHECK(buffers[0].dim(0).min() == -5);
        CHECK(buffers[0].dim(0).extent() == 10);
    }
    return 0;
}

// Each element of a Tuple-valued Func gets its own Buffer.
int tuple() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = {x * 2, cast<float>(x) / 4};
    g(x) = lut(x % 8)[0] + cast<int>(lut(x % 8)[1] * 4);
    lut.precompute();
    Buffer<int> actual = g.realize({32});
    for (int i = 0; i < 32; i++) {
        CHECK(actual(i) == (i % 8) * 3);
    }

    std::vector<Buffer<>> buffers = precomputed_buffers(g);
    CHECK(buffers.size() == 2);
    return 0;
}

// Identical Buffers are only embedded once.
int dedup() {
    Var x("x");
    {
        Func lut1("lut1"), lut2("lut2"), g("g");
        lut1(x) = x * 7;
        lut2(x) = x * 7;
        g(x) = lut1(x % 16) + lut2((x + 3) % 16);
        lut1.precompute();
        lut2.precompute();
        Buffer<int> actual = g.realize({40});
        for (int i = 0; i < 40; i++) {
            CHECK(actual(i) == (i % 16) * 7 + ((i + 3) % 16) * 7);
        }
        CHECK(precomputed_buffers(g).size() == 1);
    }
    {
        // Different contents
        Func lut1("lut1"), lut2("lut2"), g("g");
        lut1(x) = x * 7;
        lut2(x) = x * 8;
        g(x) = lut1(x % 16) + lut2(x % 16);
        lut1.precompute();
        lut2.precompute();
        CHECK(precomputed_buffers(g).size() == 2);
    }
    {
        // Same contents, different shape
        Func lut1("lut1"), lut2("lut2"), g("g");
        lut1(x) = 0;
        lut2(x) = 0;
        g(x) = lut1(x % 16) + lut2(x % 8);
        lut1.precompute();
        lut2.precompute();
        CHECK(precomputed_buffers(g).size() == 2);
    }
    return 0;
}

// Zero-dimensional embedded Buffers are mutable, so precomputed ones are
// never shared, with each other or with a user's Buffer.
int zero_dimensional() {
    Var x("x");
    Buffer<int> state = Buffer<int>::make_scalar("state");
    state() = 42;
    Func c("c"), d("d"), g("g");
    c() = 42;
    d() = 42;
    // state(std::vector<Expr>{}) is a call to state; state() would read its
    // current value.
    g(x) = c() + d() * 2 + state(std::vector<Expr>{}) * 3 + x;
    c.precompute();
    d.precompute();

    Buffer<int> actual = g.realize({4});
    for (int i = 0; i < 4; i++) {
        CHECK(actual(i) == 42 * 6 + i);
    }

    Module m = g.compile_to_module({}, "g", get_jit_target_from_environment());
    CHECK(precomputed_buffers(m).size() == 2);
    CHECK(m.buffers().size() == 3);

    // The user's Buffer is still read at run time, not aliased to a
    // precomputed one.
    state() = 7;
    actual = g.realize({4});
    for (int i = 0; i < 4; i++) {
        CHECK(actual(i) == 42 * 3 + 7 * 3 + i);
    }
    return 0;
}

// Tracing and debug_to_file on a precomputed Func don't fire when it is
// evaluated at compile time.
int tracing() {
    static int compile_time_events;
    compile_time_events = 0;
    JITHandlers handlers;
    handlers.custom_trace = [](JITUserContext *, const halide_trace_event_t *) -> int {
        compile_time_events++;
        return 0;
    };
    JITHandlers old_handlers = Internal::JITSharedRuntime::set_default_handlers(handlers);

    const std::string path = Internal::get_test_tmp_dir() + "precompute_lut.tiff";
    Internal::ensure_no_file_exists(path);

    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = x * 3;
    g(x) = lut(x % 8);
    lut.precompute().trace_stores().trace_realizations().debug_to_file(path);
    g.compile_jit();

    Internal::JITSharedRuntime::set_default_handlers(old_handlers);
    CHECK(compile_time_events == 0);
    CHECK(!Internal::file_exists(path));

    g.jit_handlers().custom_trace = [](JITUserContext *, const halide_trace_event_t *) -> int {
        return 0;
    };
    Buffer<int> actual = g.realize({16});
    for (int i = 0; i < 16; i++) {
        CHECK(actual(i) == (i % 8) * 3);
    }
    return 0;
}

// Target queries are answered for the target being compiled for, not for
// the host that evaluates the Func.
int target_query() {
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = select(target_arch_is(Target::X86), 100, 200) + x;
    g(x) = lut(x % 4);
    lut.precompute();

    for (const char *target : {"x86-64-linux", "arm-64-linux"}) {
        std::vector<Buffer<>> buffers = precomputed_buffers(g, Target(target));
        CHECK(buffers.size() == 1);
        Buffer<int> b = buffers[0];
        const int base = Target(target).arch == Target::X86 ? 100 : 200;
        for (int i = 0; i < 4; i++) {
            CHECK(b(i) == base + i);
        }
    }
    return 0;
}

// Cross-compiling a Func that computes floats warns, unless strict_float
// is used. Integer Funcs, and compiling for the host, never warn.
int float_warnings() {
    Var x("x");
    const Target cross = cross_target();
    const Target host = get_host_target();
    {
        Func lut("lut"), g("g");
        lut(x) = sin(cast<float>(x));
        g(x) = lut(x % 16);
        lut.precompute();
        CHECK(count_precompute_warnings(g, cross) == 1);
        CHECK(count_precompute_warnings(g, host) == 0);
        CHECK(count_precompute_warnings(g, cross.with_feature(Target::StrictFloat)) == 0);
    }
    {
        Func lut("lut"), g("g");
        lut(x) = strict_float(cast<float>(x) * 0.3f + 1.0f);
        g(x) = lut(x % 16);
        lut.precompute();
        CHECK(count_precompute_warnings(g, cross) == 0);
    }
    {
        // Floats are used, but don't make it into the value.
        Func lut("lut"), g("g");
        lut(x) = x * 3 + 1;
        g(x) = lut(x % 16);
        lut.precompute();
        CHECK(count_precompute_warnings(g, cross) == 0);
    }
    return 0;
}

// A precomputed Func can be read from a GPU kernel, directly or staged in
// shared memory by a wrapper.
int gpu() {
    const Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("No GPU target; skipping GPU checks\n");
        return 0;
    }
    Var x("x"), y("y"), bx("bx"), by("by"), tx("tx"), ty("ty");
    for (bool staged : {false, true}) {
        Func lut("lut"), g("g");
        lut(x) = x * 5 - 2;
        g(x, y) = lut((x + y) % 64);
        lut.precompute();
        g.gpu_tile(x, y, bx, by, tx, ty, 16, 16);
        if (staged) {
            lut.in(g).compute_at(g, bx).store_in(MemoryType::GPUShared).gpu_threads(x);
        }
        Buffer<int> actual = g.realize({64, 64}, t);
        actual.copy_to_host();
        for (int j = 0; j < 64; j++) {
            for (int i = 0; i < 64; i++) {
                CHECK(actual(i, j) == ((i + j) % 64) * 5 - 2);
            }
        }
    }
    return 0;
}

// Precomputing JIT-compiles and runs a pipeline while another pipeline is
// being compiled, possibly a JIT one.
int jit_reentrancy() {
    Var x("x");
    Func lut("lut"), g("g"), h("h");
    lut(x) = x * 11;
    g(x) = lut(x % 20);
    lut.precompute();
    for (int i = 0; i < 3; i++) {
        Buffer<int> actual = g.realize({50});
        for (int j = 0; j < 50; j++) {
            CHECK(actual(j) == (j % 20) * 11);
        }
    }

    // A different pipeline that shares the precomputed Func.
    h(x) = lut(x % 7) + 1;
    Buffer<int> actual = h.realize({50});
    for (int j = 0; j < 50; j++) {
        CHECK(actual(j) == (j % 7) * 11 + 1);
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Test {
        const char *name;
        int (*fn)();
    } tests[] = {
        {"integer_lut", integer_lut},
        {"float_lut", float_lut},
        {"calls_constant_funcs", calls_constant_funcs},
        {"update_and_rdom", update_and_rdom},
        {"precomputed_calls_precomputed", precomputed_calls_precomputed},
        {"wrapper", wrapper},
        {"padding", padding},
        {"tuple", tuple},
        {"dedup", dedup},
        {"zero_dimensional", zero_dimensional},
        {"tracing", tracing},
        {"target_query", target_query},
        {"float_warnings", float_warnings},
        {"gpu", gpu},
        {"jit_reentrancy", jit_reentrancy},
    };
    for (const Test &t : tests) {
        printf("Running %s\n", t.name);
        if (t.fn() != 0) {
            printf("%s failed\n", t.name);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
