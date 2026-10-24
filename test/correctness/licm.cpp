#include "Halide.h"

#include <cstdio>

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

int failures = 0;

void check(const char *what, const Stmt &input, const Stmt &expected) {
    Stmt result = hoist_loop_invariant_values(input);
    if (!equal(result, expected)) {
        std::cerr << "hoist_loop_invariant_values failure: " << what << "\n"
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

void test_ir() {
    Expr t = var("t"), rp0 = var("t.licm0"), rp1 = var("t.licm1");
    Expr v = opaque(x);
    Expr i = var("i");

    // Every use goes through the same op, so it folds into the let.
    check("fold shared prefix into the let",
          let("t", v, block({use(t * 4 + 3), use(t * 4 + 7), use(t * 4)})),
          let("t.licm0", v * 4, block({use(rp0 + 3), use(rp0 + 7), use(rp0)})));

    // A chain used once, not in a loop, stays inline.
    check_unchanged("single use", let("t", v, use(t * 4 + 3)));

    // A bare use keeps the original let.
    check("bare use keeps the root",
          let("t", v, block({use(t), use(t + 1), use(t + 1)})),
          let("t", v, let("t.licm0", t + 1, block({use(t), use(rp0), use(rp0)}))));

    // Two shared prefixes give two lets, in source order.
    check("two shared prefixes",
          let("t", v, block({use(t * 2), use(t * 2), use(t + 5), use(t + 5)})),
          let("t", v, let("t.licm0", t * 2, let("t.licm1", t + 5, block({use(rp0), use(rp0), use(rp1), use(rp1)})))));

    // Commutative ops are matched either way round; Sub keeps its side.
    check("commutativity and Sub on the right",
          let("t", v, block({use(t * 4), use(4 * t), use(5 - t), use(5 - t)})),
          let("t", v, let("t.licm0", t * 4, let("t.licm1", 5 - t, block({use(rp0), use(rp0), use(rp1), use(rp1)})))));

    // The arithmetic ops all take part.
    check("arithmetic ops",
          let("t", v, block({use(t / 3 % 5), use(t / 3 % 5), use(min(t, 8)), use(min(t, 8)), use(max(t, x) - y), use(max(t, x) - y)})),
          let("t", v, let("t.licm0", t / 3 % 5, let("t.licm1", min(t, 8), let("t.licm2", max(t, x) - y, block({use(rp0), use(rp0), use(rp1), use(rp1), use(var("t.licm2")), use(var("t.licm2"))}))))));

    // Shared uses inside a loop: the let goes just around the loop.
    check("shared uses in a loop",
          let("t", v, block({use(t), loop("i", y, block({use(t * 4), use(t * 4)}))})),
          let("t", v, block({use(t), let("t.licm0", t * 4, loop("i", y, block({use(rp0), use(rp0)})))})));

    // A use in a loop bound lies outside the loop body; the let wraps the loop.
    check("loop bound use",
          let("t", v, block({use(t), loop("i", t * 4, use(t * 4))})),
          let("t", v, block({use(t), let("t.licm0", t * 4, loop("i", rp0, use(rp0)))})));

    // A chain used once is bound when that takes it out of a loop, and folds
    // into the let when the variable has no other use. A trailing integer
    // constant stays at the use: loops track those as offsets.
    check("single use out of a loop, folded",
          let("t", v, loop("i", y, use(t * 4 + 1))),
          let("t.licm0", v * 4, loop("i", y, use(rp0 + 1))));
    check("single use out of a loop, root kept",
          let("t", v, block({use(t), loop("i", y, use(t + x))})),
          let("t", v, block({use(t), let("t.licm0", t + x, loop("i", y, use(rp0)))})));
    check_unchanged("trailing constant alone is not hoisted",
                    let("t", v, block({use(t), loop("i", y, use(t + 1))})));

    // Not above anything else than loops: uses in an if branch stay in it.
    check("placement stays inside an if branch",
          let("t", v, block({use(t), IfThenElse::make(x < y, loop("i", y, use(t * 4)))})),
          let("t", v, block({use(t), IfThenElse::make(x < y, let("t.licm0", t * 4, loop("i", y, use(rp0))))})));

    // A loop variable is a root too: the chain belongs to it, as it is bound
    // inside t, and its let goes inside the loop.
    check("loop variable root",
          let("t", v, loop("i", y, block({use(t + i), use(t + i)}))),
          let("t", v, loop("i", y, let("i.licm0", i + t, block({use(var("i.licm0")), use(var("i.licm0"))})))));

    // A loop variable's chain hoists out of an inner loop, to the top of its
    // own loop body.
    check("loop variable chain out of an inner loop",
          loop("i", y, loop("j", y, use(i * 4 + var("j")))),
          loop("i", y, let("i.licm0", i * 4, loop("j", y, use(var("i.licm0") + var("j"))))));

    // So is a free variable; two of them are ordered by first appearance.
    check("free variable root",
          block({use(x + y), use(y + x), use(x)}),
          let("x.licm0", x + y, block({use(var("x.licm0")), use(var("x.licm0")), use(x)})));
    check("free variable chain out of a loop",
          loop("i", y, use(x * 3 + i)),
          let("x.licm0", x * 3, loop("i", y, use(var("x.licm0") + i))));

    // A let whose value is a chain of another root is an alias of that chain,
    // so lets with the same value share one trie. Once the chain has a let of
    // its own the alias is a rename and goes away.
    Expr u = var("u"), w = var("w");
    check("lets with the same value",
          block({let("u", x + 5, use(u % 8 * y)), let("w", x + 5, use(w % 8 * y))}),
          let("x.licm0", x + 5, let("x.licm1", (var("x.licm0") % 8) * y, block({use(var("x.licm1")), use(var("x.licm1"))}))));
    check("alias let substituted",
          let("u", x + 5, block({use(u), use(u * 2), use(u * 2)})),
          let("x.licm0", x + 5, let("x.licm1", var("x.licm0") * 2, block({use(var("x.licm0")), use(var("x.licm1")), use(var("x.licm1"))}))));

    // An operation on two aliases of the same root is a chain of that root,
    // with the second alias's chain as the operand, so it is shared and its
    // let is built from the chains, not from the alias names.
    check("operation on two aliases",
          let("u", x + 1, let("w", x + 2, block({use(w - u), use(w - u)}))),
          let("x.licm0", x + 1, let("x.licm1", x + 2, let("x.licm2", var("x.licm1") - var("x.licm0"), block({use(var("x.licm2")), use(var("x.licm2"))})))));

    // With two lets, the chain belongs to the inner one.
    check("chain of the inner root",
          let("t", v, let("u", opaque(y), block({use(t + var("u")), use(t + var("u"))}))),
          let("t", v, let("u.licm0", opaque(y) + t, block({use(var("u.licm0")), use(var("u.licm0"))}))));

    // A chain of a root bound further out may be an operand, so a whole
    // expression over several variables can be shared. The operand's two
    // occurrences are shared as well, by a let placed before its user's.
    check("chain as operand",
          let("t", v, block({use(t), use(t * 2 + x * 3), use(t * 2 + x * 3)})),
          let("t", v, let("x.licm0", x * 3, let("t.licm0", t * 2 + var("x.licm0"), block({use(t), use(rp0), use(rp0)})))));

    // An invariant expression over several variables hoists as a whole, and
    // the let is built with the lets visible where it lands.
    check("operand chain hoisted with its user",
          block({use(x * 3), let("t", v, loop("i", y, use((t * 2 + x * 3) * i)))}),
          let("x.licm0", x * 3, block({use(var("x.licm0")), let("t.licm0", v * 2 + var("x.licm0"), loop("i", y, use(rp0 * i)))})));

    // A let injected at the root's body may mention the root in an operand,
    // here through the folded chain: it is built while the root is bound.
    check("operand mentions a folded root",
          let("t", v, block({use((t + 1) * (t + 1)), use((t + 1) * (t + 1))})),
          let("t.licm0", v + 1, let("t.licm1", rp0 * rp0, block({use(rp1), use(rp1)}))));

    // The root may appear inside an operand too, since that operand is
    // available where the root is bound.
    check("root inside its own operand",
          let("t", v, block({use(t), use(t * t + 1), use(t * t + 1)})),
          let("t", v, let("t.licm0", t * t + 1, block({use(t), use(rp0), use(rp0)}))));

    // Any pure node continues a chain, so the select is a chain of b (the
    // innermost of its variables) and folds into b's let. The comparison in
    // it is a chain of x shared by both sites, and its let is lifted above
    // the folded let that needs it.
    Expr a = var("a"), b = var("b");
    Expr sel = select(x < y, a, b);
    check("select as a chain node",
          let("a", opaque(x), let("b", opaque(y), let("t", v, block({use(t), use((t + sel) * 2), use((t + sel) * 2)})))),
          let("a", opaque(x), let("x.licm0", x < y, let("b.licm0", select(Variable::make(Bool(), "x.licm0"), a, opaque(y)), let("t", v, let("t.licm0", (t + var("b.licm0")) * 2, block({use(t), use(rp0), use(rp0)})))))));

    // Narrowing casts and pure calls chain; a widening cast does not, since
    // codegen folds it into the operation around it.
    check("narrowing cast and pure call",
          let("t", v, block({use(cast<int16_t>(t) * 2), use(cast<int16_t>(t) * 2), use(abs(t)), use(abs(t))})),
          let("t", v, let("t.licm0", cast<int16_t>(t) * 2, let("t.licm1", abs(t), block({use(Variable::make(Int(16), "t.licm0")), use(Variable::make(Int(16), "t.licm0")), use(Variable::make(UInt(32), "t.licm1")), use(Variable::make(UInt(32), "t.licm1"))})))));
    check_unchanged("widening cast ends the chain",
                    let("t", v, block({use(cast<int64_t>(t) + 1), use(cast<int64_t>(t) + 1)})));

    // A pure call on the root is a chain node, so it hoists like arithmetic.
    Expr call_t = Call::make(Int(32), "pure_fn", {t, 1}, Call::PureExtern);
    check("pure call hoisted out of a loop",
          let("t", v, block({use(t), loop("i", y, use(call_t + i))})),
          let("t", v, block({use(t), let("t.licm0", call_t, loop("i", y, use(rp0 + i)))})));

    // A pure call over constants alone is hoisted out of the loop, as it
    // depends on nothing, but no further than the loop.
    Expr pure_call = Call::make(Int(32), "pure_fn", {0, 1}, Call::PureExtern);
    check("constant pure call out of a loop",
          loop("i", y, use(pure_call + i)),
          let("const.licm0", pure_call, loop("i", y, use(var("const.licm0") + i))));
    check("constant pure call stays inside an if",
          IfThenElse::make(x < y, loop("i", y, use(pure_call + i))),
          IfThenElse::make(x < y, let("const.licm0", pure_call, loop("i", y, use(var("const.licm0") + i)))));

    // A let of a constant expression is an alias of the constant root's
    // chain; a chain continuing from it starts from that alias.
    check("chain from an alias of a constant expression",
          let("k", pure_call, block({use(min(var("k"), 0)), use(min(var("k"), 0))})),
          let("const.licm0", pure_call, let("const.licm1", min(var("const.licm0"), 0), block({use(var("const.licm1")), use(var("const.licm1"))}))));

    // A load can't move, so it ends the chain.
    Expr load = Load::make(Int(32), "buf", x);
    check_unchanged("load as operand",
                    let("t", v, block({use(t), use(t + load), use(t + load)})));

    // Vectors are left alone.
    Expr vt = Variable::make(Int(32, 4), "t");
    check_unchanged("vector root",
                    let("t", Broadcast::make(x, 4), block({use(vt * 4), use(vt * 4)})));

    // Lets bound by a Let expression are handled too, inside their own body.
    Expr q = var("q"), qrp0 = var("q.licm0");
    check("Let expression root",
          use(Let::make("q", v, (q * 4 + 1) + (q * 4 + 2))),
          use(Let::make("q.licm0", v * 4, (qrp0 + 1) + (qrp0 + 2))));

    // Acquires stay directly under their For; the let goes inside them.
    Expr sem = Variable::make(type_of<halide_semaphore_t *>(), "sem");
    check("acquire under a for",
          let("t", v, block({use(t), loop("i", y, Acquire::make(sem, 1, block({use(t * 4), use(t * 4)})))})),
          let("t", v, block({use(t), let("t.licm0", t * 4, loop("i", y, Acquire::make(sem, 1, block({use(rp0), use(rp0)}))))})));

    // An Acquire count is evaluated outside the loop body, so the loop
    // variable in it is not a root there: nothing to bind, nothing to place
    // above the loop variable's own scope.
    check_unchanged("loop variable in an acquire count",
                    let("t", v, loop("i", y, Acquire::make(sem, i + t, use(t)))));

    // A For directly under a Fork stays there; the let goes above the Fork.
    check("for under a fork",
          let("t", v, block({use(t), Fork::make(loop("i", t * 4, use(t * 4), ForType::Parallel), use(x))})),
          let("t", v, let("t.licm0", t * 4, block({use(t), Fork::make(loop("i", rp0, use(rp0), ForType::Parallel), use(x))}))));

    // A parallel loop is a loop like any other: the let goes around it and is
    // captured by the closure once, rather than computed per iteration.
    check("parallel loop",
          let("t", v, block({use(t), loop("i", y, block({use(t + x), use(t + x)}), ForType::Parallel)})),
          let("t", v, block({use(t), let("t.licm0", t + x, loop("i", y, block({use(rp0), use(rp0)}), ForType::Parallel))})));

    // Uses inside a gpu kernel get their own let inside the kernel rather than
    // a new kernel argument; the single host use stays inline.
    auto kernel = [&](const Expr &thread_max, const Stmt &body) {
        return loop("bx", 7, loop("tx", thread_max, body, ForType::GPUThread, DeviceAPI::CUDA),
                    ForType::GPUBlock, DeviceAPI::CUDA);
    };
    check("gpu kernel region",
          let("t", v, block({use(t), use(t * 4), kernel(7, block({use(t * 4), use(t * 4)}))})),
          let("t", v, block({use(t), use(t * 4), kernel(7, let("t.licm0", t * 4, block({use(rp0), use(rp0)})))})));

    // A gpu loop bound is evaluated by the host, so a use there is a host use,
    // and nothing is bound between the gpu loops.
    check("gpu loop bound use",
          let("t", v, block({use(t), use(t * 4), kernel(t * 4, use(t * 4))})),
          let("t", v, let("t.licm0", t * 4, block({use(t), use(rp0), kernel(rp0, use(t * 4))}))));

    // Injected lets must not disturb how the two passes compare binding
    // depths: c is bound after a's injected let, and b's chain uses c.
    Expr c = var("c");
    check("binding depths with injected lets",
          let("a", opaque(x), block({use(a), use(a + 1), use(a + 1), let("c", opaque(y), let("b", opaque(x + y), block({use(c + b), use(c + b)})))})),
          let("a", opaque(x), let("a.licm0", a + 1, block({use(a), use(var("a.licm0")), use(var("a.licm0")), let("c", opaque(y), let("b.licm0", opaque(x + y) + c, block({use(var("b.licm0")), use(var("b.licm0"))})))}))));

    // Names the input already binds are never reused, as when the pass runs
    // on IR it has rewritten before. e.licm1 here is a bool alias of e < 5,
    // which gets a let of its own once it is shared with the assert.
    Expr e = var("e"), e1 = Variable::make(Bool(), "e.licm1");
    Expr bq = Call::make(Bool(), "bq", {}, Call::Extern);
    check("no new name that is already bound",
          let("e", v, let("e.licm1", e < 5, block({AssertStmt::make(!bq || e1, 0), use(e * 2 + 1), use(e * 2 + 2), use(e + 5), use(e + 5)}))),
          let("e", v, let("e.licm0", e < 5, let("e.licm2", e * 2, let("e.licm3", e + 5, block({AssertStmt::make(!bq || Variable::make(Bool(), "e.licm0"), 0), use(var("e.licm2") + 1), use(var("e.licm2") + 2), use(var("e.licm3")), use(var("e.licm3"))}))))));
}

// The pass runs in every lowering, so a pipeline with plenty of repeated index
// arithmetic must still compute the right thing.
void test_pipeline() {

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
                printf("Pipeline mismatch at (%d, %d): %d instead of %d\n", i, j, out(i, j), expected);
                failures++;
                return;
            }
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_ir();
    test_pipeline();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
