#include "Halide.h"
#include <stdio.h>

// Tests the lowering pass that binds a variable's value modulo a constant next
// to the let that defines it. See InjectModuloVars.h.

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Pull the argument back out of the sink() call the statements below wrap
// around the expression under test.
class FindSinkArg : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == "sink") {
            arg = op->args[0];
        }
    }

public:
    Expr arg;
};

Expr run(const Expr &e, const std::vector<Expr> &asserts, const Expr &let_value) {
    Expr x = Variable::make(Int(32), "x");
    // Mention x a second time so the simplifier won't just inline the let and
    // make the whole exercise moot -- that is exactly the situation this pass
    // exists for.
    Stmt s = Evaluate::make(Call::make(Int(32), "sink", {e, x}, Call::Extern));
    s = LetStmt::make("x", let_value, s);
    for (auto it = asserts.rbegin(); it != asserts.rend(); it++) {
        s = Block::make(AssertStmt::make(*it, 0), s);
    }
    s = simplify(inject_modulo_vars(s));
    FindSinkArg finder;
    s.accept(&finder);
    return finder.arg;
}

int check(const char *name, const Expr &got, const Expr &expected) {
    if (!equal(got, expected)) {
        std::cerr << name << ": got " << got << ", expected " << expected << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");
    Expr xo = Variable::make(Int(32), "xo");
    Expr off = Variable::make(Int(32), "off");
    std::vector<Expr> off_is_small{0 <= off && off <= 1};

    // An aligned split's loop variable, used with the alignment subtracted
    // back off. x is congruent to off modulo two, so this is zero. The extra
    // term keeps the simplifier from peeling the "+ off" off the let by
    // itself, which is what it does when the value ends in a bare variable.
    Expr yo = Variable::make(Int(32), "yo");
    Expr aligned = xo * 16 + (yo * 2 + off);
    int result = 0;
    result |= check("cancel", run((x - off) % 2, off_is_small, aligned), 0);
    // The same thing written the other way round, and with an offset.
    result |= check("cancel, added", run((x + off) % 2, off_is_small, aligned), 0);
    result |= check("cancel, +1", run((x - off + 1) % 2, off_is_small, aligned), 1);

    // The variable reached through a multiply is still only needed mod 2.
    result |= check("through a multiply", run((3 * x - 3 * off) % 2, off_is_small, aligned), 0);

    // Without the assert there's nothing to say off is its own remainder, so
    // this must be left alone rather than rewritten into something wrong.
    {
        Expr got = run((x - off) % 2, {}, aligned);
        if (is_const(got)) {
            std::cerr << "unbounded offset: should not have folded, got " << got << "\n";
            result = 1;
        }
    }

    // A congruence modulo 2 says nothing about the value modulo 3 here, since
    // 16 is not a multiple of 3.
    {
        Expr got = run((x - off) % 3, off_is_small, aligned);
        if (is_const(got)) {
            std::cerr << "wrong modulus: should not have folded, got " << got << "\n";
            result = 1;
        }
    }

    // Division is not congruence-preserving, so nothing may be substituted
    // through it.
    {
        Expr got = run((x / 2) % 2, off_is_small, aligned);
        if (is_const(got)) {
            std::cerr << "through a divide: should not have folded, got " << got << "\n";
            result = 1;
        }
    }

    if (result) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
