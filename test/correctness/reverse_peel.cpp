#include "Halide.h"

#include <cstdio>
#include <cstdlib>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Free variables stand for pipeline parameters.
const Expr x = Variable::make(Int(32), "x");
const Expr y = Variable::make(Int(32), "y");

Expr var(const std::string &name) {
    return Variable::make(Int(32), name);
}

// A value the pass treats as a black box.
Expr opaque(const Expr &e) {
    return Call::make(Int(32), "opaque", {e}, Call::Extern);
}

// A use of an expression that can't be simplified away.
Stmt use(const Expr &e) {
    return Evaluate::make(Call::make(Int(32), "use", {e}, Call::Extern));
}

Stmt loop(const std::string &name, const Expr &max, const Stmt &body,
          ForType for_type = ForType::Serial, DeviceAPI device_api = DeviceAPI::None) {
    return For::make(name, 0, max, for_type, Partition::Auto, device_api, body);
}

Stmt let(const std::string &name, const Expr &value, const Stmt &body) {
    return LetStmt::make(name, value, body);
}

Stmt block(const std::vector<Stmt> &stmts) {
    return Block::make(stmts);
}

void set_env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void set_policy(const char *placement, bool parallel_regions = false) {
    set_env("HL_REVERSE_PEEL_POLICY", placement);
    set_env("HL_REVERSE_PEEL_PARALLEL_REGIONS", parallel_regions ? "1" : "0");
}

int failures = 0;

void check(const char *what, const Stmt &input, const Stmt &expected) {
    Stmt result = reverse_peel_lets(input);
    if (!equal(result, expected)) {
        std::cerr << "reverse_peel_lets failure: " << what << "\n"
                  << "Input:\n"
                  << input
                  << "Output:\n"
                  << result
                  << "Expected:\n"
                  << expected << "\n";
        failures++;
    }
}

void check_unchanged(const char *what, const Stmt &input) {
    check(what, input, input);
}

void test_sink() {
    set_policy("sink");

    Expr t = var("t"), rp0 = var("t.rp0"), rp1 = var("t.rp1");
    Expr v = opaque(x);

    // Every use goes through the same op, so it folds into the let.
    check("fold shared prefix into the let",
          let("t", v, block({use(t * 4 + 3), use(t * 4 + 7), use(t * 4)})),
          let("t.rp0", v * 4, block({use(rp0 + 3), use(rp0 + 7), use(rp0)})));

    // A chain used once stays inline.
    check_unchanged("single use", let("t", v, use(t * 4 + 3)));

    // A bare use keeps the original let.
    check("bare use keeps the root",
          let("t", v, block({use(t), use(t + 1), use(t + 1)})),
          let("t", v, let("t.rp0", t + 1, block({use(t), use(rp0), use(rp0)}))));

    // Two shared prefixes give two lets, in source order.
    check("two shared prefixes",
          let("t", v, block({use(t * 2), use(t * 2), use(t + 5), use(t + 5)})),
          let("t", v, let("t.rp0", t * 2, let("t.rp1", t + 5, block({use(rp0), use(rp0), use(rp1), use(rp1)})))));

    // Commutative ops are matched either way round; Sub keeps its side.
    check("commutativity and Sub on the right",
          let("t", v, block({use(t * 4), use(4 * t), use(5 - t), use(5 - t)})),
          let("t", v, let("t.rp0", t * 4, let("t.rp1", 5 - t, block({use(rp0), use(rp0), use(rp1), use(rp1)})))));

    // All seven ops take part.
    check("all ops",
          let("t", v, block({use(t / 3 % 5), use(t / 3 % 5), use(min(t, 8)), use(min(t, 8)), use(max(t, x) - y), use(max(t, x) - y)})),
          let("t", v, let("t.rp0", t / 3 % 5, let("t.rp1", min(t, 8), let("t.rp2", max(t, x) - y, block({use(rp0), use(rp0), use(rp1), use(rp1), use(var("t.rp2")), use(var("t.rp2"))}))))));

    // The shared let sinks to the innermost loop body holding its uses.
    check("sink into a loop",
          let("t", v, block({use(t), loop("i", y, block({use(t * 4), use(t * 4)}))})),
          let("t", v, block({use(t), loop("i", y, let("t.rp0", t * 4, block({use(rp0), use(rp0)})))})));

    // A use in a loop bound lies outside the loop body; the let wraps the loop.
    check("loop bound use",
          let("t", v, block({use(t), loop("i", t * 4, use(t * 4))})),
          let("t", v, block({use(t), let("t.rp0", t * 4, loop("i", rp0, use(rp0)))})));

    // A loop variable is a root too: the chain belongs to it, as it is bound
    // inside t, and its let goes inside the loop.
    Expr i = var("i");
    check("loop variable root",
          let("t", v, loop("i", y, block({use(t + i), use(t + i)}))),
          let("t", v, loop("i", y, let("i.rp0", i + t, block({use(var("i.rp0")), use(var("i.rp0"))})))));

    // So is a free variable; two of them are ordered by first appearance.
    check("free variable root",
          block({use(x + y), use(y + x), use(x)}),
          let("x.rp0", x + y, block({use(var("x.rp0")), use(var("x.rp0")), use(x)})));

    // With two lets, the chain belongs to the inner one.
    check("chain of the inner root",
          let("t", v, let("u", opaque(y), block({use(t + var("u")), use(t + var("u"))}))),
          let("t", v, let("u.rp0", opaque(y) + t, block({use(var("u.rp0")), use(var("u.rp0"))}))));

    // A chain of a root bound further out may be an operand, so a whole
    // expression over several variables can be shared. The operand's two
    // occurrences are shared as well, by a let placed before its user's.
    check("chain as operand",
          let("t", v, block({use(t), use(t * 2 + x * 3), use(t * 2 + x * 3)})),
          let("t", v, let("x.rp0", x * 3, let("t.rp0", t * 2 + var("x.rp0"), block({use(t), use(rp0), use(rp0)})))));

    // The root may appear inside an operand too, since that operand is
    // available where the root is bound.
    check("root inside its own operand",
          let("t", v, block({use(t), use(t * t + 1), use(t * t + 1)})),
          let("t", v, let("t.rp0", t * t + 1, block({use(t), use(rp0), use(rp0)}))));

    // Any pure node continues a chain, so the select is a chain of b (the
    // innermost of its variables) and folds into b's let, and the comparison
    // inside it, once shared by a let of x, is left with no use and dropped.
    Expr a = var("a"), b = var("b");
    Expr sel = select(x < y, a, b);
    check("select as a chain node",
          let("a", opaque(x), let("b", opaque(y), let("t", v, block({use(t), use((t + sel) * 2), use((t + sel) * 2)})))),
          let("a", opaque(x), let("b.rp0", select(x < y, a, opaque(y)), let("t", v, let("t.rp0", (t + var("b.rp0")) * 2, block({use(t), use(rp0), use(rp0)}))))));

    // Narrowing casts and pure calls chain; a widening cast does not, since
    // codegen folds it into the operation around it.
    check("narrowing cast and pure call",
          let("t", v, block({use(cast<int16_t>(t) * 2), use(cast<int16_t>(t) * 2), use(abs(t)), use(abs(t))})),
          let("t", v, let("t.rp0", cast<int16_t>(t) * 2, let("t.rp1", abs(t), block({use(Variable::make(Int(16), "t.rp0")), use(Variable::make(Int(16), "t.rp0")), use(Variable::make(UInt(32), "t.rp1")), use(Variable::make(UInt(32), "t.rp1"))})))));
    check_unchanged("widening cast ends the chain",
                    let("t", v, block({use(cast<int64_t>(t) + 1), use(cast<int64_t>(t) + 1)})));

    // A load can't move, so it ends the chain.
    Expr load = Load::make(Int(32), "buf", x);
    check_unchanged("load as operand",
                    let("t", v, block({use(t), use(t + load), use(t + load)})));

    // Vectors are left alone.
    Expr vt = Variable::make(Int(32, 4), "t");
    check_unchanged("vector root",
                    let("t", Broadcast::make(x, 4), block({use(vt * 4), use(vt * 4)})));

    // Lets bound by a Let expression are handled too, inside their own body.
    Expr q = var("q"), qrp0 = var("q.rp0");
    check("Let expression root",
          use(Let::make("q", v, (q * 4 + 1) + (q * 4 + 2))),
          use(Let::make("q.rp0", v * 4, (qrp0 + 1) + (qrp0 + 2))));

    // Acquires stay directly under their For; the let goes inside them.
    Expr sem = Variable::make(type_of<halide_semaphore_t *>(), "sem");
    check("acquire under a for",
          let("t", v, block({use(t), loop("i", y, Acquire::make(sem, 1, block({use(t * 4), use(t * 4)})))})),
          let("t", v, block({use(t), loop("i", y, Acquire::make(sem, 1, let("t.rp0", t * 4, block({use(rp0), use(rp0)}))))})));

    // An Acquire count is evaluated outside the loop body, so the loop
    // variable in it is not a root there: nothing to bind, nothing to place
    // above the loop variable's own scope.
    check_unchanged("loop variable in an acquire count",
                    let("t", v, loop("i", y, Acquire::make(sem, var("i") + t, use(t)))));

    // A For directly under a Fork stays there; the let goes above the Fork.
    check("for under a fork",
          let("t", v, block({use(t), Fork::make(loop("i", t * 4, use(t * 4), ForType::Parallel), use(x))})),
          let("t", v, let("t.rp0", t * 4, block({use(t), Fork::make(loop("i", rp0, use(rp0), ForType::Parallel), use(x))}))));

    // Uses inside a gpu kernel get their own let inside the kernel rather than
    // a new kernel argument; the single host use stays inline.
    auto kernel = [&](const Expr &thread_max, const Stmt &body) {
        return loop("bx", 7, loop("tx", thread_max, body, ForType::GPUThread, DeviceAPI::CUDA),
                    ForType::GPUBlock, DeviceAPI::CUDA);
    };
    check("gpu kernel region",
          let("t", v, block({use(t), use(t * 4), kernel(7, block({use(t * 4), use(t * 4)}))})),
          let("t", v, block({use(t), use(t * 4), kernel(7, let("t.rp0", t * 4, block({use(rp0), use(rp0)})))})));

    // A gpu loop bound is evaluated by the host, so a use there is a host use,
    // and nothing is bound between the gpu loops.
    check("gpu loop bound use",
          let("t", v, block({use(t), use(t * 4), kernel(t * 4, use(t * 4))})),
          let("t", v, let("t.rp0", t * 4, block({use(t), use(rp0), kernel(rp0, use(t * 4))}))));

    // Injected lets must not disturb how the two passes compare binding
    // depths: c is bound after a's injected let, and b's chain uses c.
    Expr c = var("c");
    check("binding depths with injected lets",
          let("a", opaque(x), block({use(a), use(a + 1), use(a + 1), let("c", opaque(y), let("b", opaque(x + y), block({use(c + b), use(c + b)})))})),
          let("a", opaque(x), let("a.rp0", a + 1, block({use(a), use(var("a.rp0")), use(var("a.rp0")), let("c", opaque(y), let("b.rp0", opaque(x + y) + c, block({use(var("b.rp0")), use(var("b.rp0"))})))}))));
}

void test_hoist() {
    set_policy("hoist");

    Expr t = var("t"), rp0 = var("t.rp0");
    Expr v = opaque(x);
    Expr i = var("i");

    // Shared chains go as far out as the root allows, not to the innermost
    // common scope.
    check("hoist out of a loop",
          let("t", v, block({use(t), loop("i", y, block({use(t * 4), use(t * 4)}))})),
          let("t", v, let("t.rp0", t * 4, block({use(t), loop("i", y, block({use(rp0), use(rp0)}))}))));

    // A chain used once is hoisted when that takes it out of a loop, and
    // folds into the let when the variable has no other use. A trailing
    // integer constant stays at the use: loops track those as offsets.
    check("hoist a single use out of a loop",
          let("t", v, loop("i", y, use(t * 4 + 1))),
          let("t.rp0", v * 4, loop("i", y, use(rp0 + 1))));
    check("hoist a single use, root kept",
          let("t", v, block({use(t), loop("i", y, use(t + x))})),
          let("t", v, let("t.rp0", t + x, block({use(t), loop("i", y, use(rp0))}))));
    check_unchanged("trailing constant alone is not hoisted",
                    let("t", v, block({use(t), loop("i", y, use(t + 1))})));
    check_unchanged("single use not in a loop", let("t", v, use(t * 4 + 1)));

    // A pure call over constants alone is hoisted out of the loop, as it
    // depends on nothing, but not out of the if: the work stays on the paths
    // that need it.
    Expr pure_call = Call::make(Int(32), "pure_fn", {0, 1}, Call::PureExtern);
    check("constant pure call out of a loop",
          loop("i", y, use(pure_call + i)),
          let("const.rp0", pure_call, loop("i", y, use(var("const.rp0") + i))));
    // A pure call on the root is a chain node, so it hoists like arithmetic.
    Expr call_t = Call::make(Int(32), "pure_fn", {t, 1}, Call::PureExtern);
    check("pure call hoisted out of a loop",
          let("t", v, block({use(t), loop("i", y, use(call_t + i))})),
          let("t", v, let("t.rp0", call_t, block({use(t), loop("i", y, use(rp0 + i))}))));
    check("hoisting stops at an if",
          IfThenElse::make(x < y, loop("i", y, use(pure_call + i))),
          IfThenElse::make(x < y, let("const.rp0", pure_call, loop("i", y, use(var("const.rp0") + i)))));

    // A loop variable's chain hoists to the top of its own loop body.
    check("loop variable chain out of an inner loop",
          loop("i", y, loop("j", y, use(i * 4 + var("j")))),
          loop("i", y, let("i.rp0", i * 4, loop("j", y, use(var("i.rp0") + var("j"))))));

    // A free variable's chain hoists to the top of the pipeline.
    check("free variable chain out of a loop",
          loop("i", y, use(x * 3 + i)),
          let("x.rp0", x * 3, loop("i", y, use(var("x.rp0") + i))));

    // An invariant expression over several variables hoists as a whole, and
    // the let is built with the lets visible where it lands.
    check("operand chain hoisted with its user",
          let("t", v, block({use(x * 3), loop("i", y, use((t * 2 + x * 3) * i))})),
          let("x.rp0", x * 3, let("t.rp0", v * 2 + var("x.rp0"), block({use(var("x.rp0")), loop("i", y, use(rp0 * i))}))));

    // Parallel loop bodies as regions: nothing new crosses into the closure.
    Stmt par = let("t", v, block({use(t), loop("i", y, block({use(t + 1), use(t + 1)}), ForType::Parallel)}));
    check("hoist across a parallel loop",
          par,
          let("t", v, let("t.rp0", t + 1, block({use(t), loop("i", y, block({use(rp0), use(rp0)}), ForType::Parallel)}))));
    set_policy("hoist", true);
    check("parallel loop as a region",
          par,
          let("t", v, block({use(t), loop("i", y, let("t.rp0", t + 1, block({use(rp0), use(rp0)})), ForType::Parallel)})));

    set_policy("sink");
}

// The pass runs in every lowering, so a pipeline with plenty of repeated index
// arithmetic must still compute the right thing.
void test_pipeline(const char *placement, bool parallel_regions) {
    set_policy(placement, parallel_regions);

    ImageParam in(UInt(8), 2, "in");
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");
    f(x, y) = cast<uint16_t>(in(x, y)) + cast<uint16_t>(in(x + 1, y)) + cast<uint16_t>(in(x, y + 1));
    g(x, y) = f(x, y) * 3 + f(x + 2, y) + f(x, y + 2);
    h(x, y) = cast<uint8_t>((g(x, y) + g(x + 1, y + 1)) / 9);

    h.compute_root().parallel(y).vectorize(x, 8);
    g.compute_at(h, y).vectorize(x, 8);
    f.compute_at(h, y).vectorize(x, 8);

    Buffer<uint8_t> input(64, 48);
    for (int j = 0; j < 48; j++) {
        for (int i = 0; i < 64; i++) {
            input(i, j) = (uint8_t)(i * 7 + j * 13);
        }
    }
    in.set(input);
    Buffer<uint8_t> out = h.realize({60, 44});

    auto F = [&](int x, int y) { return (int)input(x, y) + input(x + 1, y) + input(x, y + 1); };
    auto G = [&](int x, int y) { return F(x, y) * 3 + F(x + 2, y) + F(x, y + 2); };
    for (int j = 0; j < 44; j++) {
        for (int i = 0; i < 60; i++) {
            uint8_t expected = (uint8_t)((uint16_t)(G(i, j) + G(i + 1, j + 1)) / 9);
            if (out(i, j) != expected) {
                printf("Pipeline mismatch (%s) at (%d, %d): %d instead of %d\n", placement, i, j, out(i, j), expected);
                failures++;
                return;
            }
        }
    }
    set_policy("sink");
}

}  // namespace

int main(int argc, char **argv) {
    test_sink();
    test_hoist();
    test_pipeline("sink", false);
    test_pipeline("hoist", false);
    test_pipeline("hoist", true);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
