#include "ModulusRemainder.h"

#include "IR.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

// A version of mod where a % 0 == a
int64_t mod(int64_t a, int64_t b) {
    if (b == 0) {
        return a;
    } else {
        return mod_imp(a, b);
    }
}

class ComputeModulusRemainder : public IRVisitor {
public:
    ModulusRemainder analyze(const Expr &e);

    ModulusRemainder result;
    Scope<ModulusRemainder> scope;

    ComputeModulusRemainder(const Scope<ModulusRemainder> *s) {
        scope.set_containing_scope(s);
    }

    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Variable *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Acquire *) override;
    void visit(const Store *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Realize *) override;
    void visit(const Block *) override;
    void visit(const Fork *) override;
    void visit(const IfThenElse *) override;
    void visit(const Free *) override;
    void visit(const Evaluate *) override;
    void visit(const Shuffle *) override;
    void visit(const VectorReduce *) override;
    void visit(const Prefetch *) override;
    void visit(const Atomic *) override;
};

void ComputeModulusRemainder::visit(const IntImm *op) {
    // Equal to op->value modulo anything. We'll use zero as the
    // modulus to mark this special case. We'd better be able to
    // handle zero in the rest of the code...
    result = {0, op->value};
}

void ComputeModulusRemainder::visit(const UIntImm *op) {
    internal_error << "modulus_remainder of uint\n";
}

void ComputeModulusRemainder::visit(const FloatImm *) {
    internal_error << "modulus_remainder of float\n";
}

void ComputeModulusRemainder::visit(const StringImm *) {
    internal_error << "modulus_remainder of string\n";
}

void ComputeModulusRemainder::visit(const Cast *) {
    // TODO: Could probably do something reasonable for integer
    // upcasts and downcasts where the modulus is a power of two.
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const Reinterpret *) {
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const Variable *op) {
    if (scope.contains(op->name)) {
        result = scope.get(op->name);
    } else {
        result = ModulusRemainder{};
    }
}

void ComputeModulusRemainder::visit(const Add *op) {
    result = analyze(op->a) + analyze(op->b);
}

void ComputeModulusRemainder::visit(const Sub *op) {
    result = analyze(op->a) - analyze(op->b);
}

void ComputeModulusRemainder::visit(const Mul *op) {
    result = analyze(op->a) * analyze(op->b);
}

void ComputeModulusRemainder::visit(const Div *op) {
    result = analyze(op->a) / analyze(op->b);
}

void ComputeModulusRemainder::visit(const Min *op) {
    result = ModulusRemainder::unify(analyze(op->a), analyze(op->b));
}

void ComputeModulusRemainder::visit(const Max *op) {
    result = ModulusRemainder::unify(analyze(op->a), analyze(op->b));
}

void ComputeModulusRemainder::visit(const EQ *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const NE *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const LT *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const LE *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const GT *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const GE *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const And *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const Or *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const Not *) {
    internal_error << "modulus_remainder of bool\n";
}

void ComputeModulusRemainder::visit(const Select *op) {
    result = ModulusRemainder::unify(analyze(op->true_value),
                                     analyze(op->false_value));
}

void ComputeModulusRemainder::visit(const Load *) {
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const Ramp *) {
    internal_error << "modulus_remainder of vector\n";
}

void ComputeModulusRemainder::visit(const Broadcast *) {
    internal_error << "modulus_remainder of vector\n";
}

void ComputeModulusRemainder::visit(const Call *) {
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const Let *op) {
    if (op->value.type().is_int()) {
        ScopedBinding<ModulusRemainder> bind(scope, op->name, analyze(op->value));
        result = analyze(op->body);
    } else {
        result = analyze(op->body);
    }
}

void ComputeModulusRemainder::visit(const Shuffle *op) {
    // It's possible that scalar expressions are extracting a lane of
    // a vector - don't fail in this case, but stop
    internal_assert(op->indices.size() == 1) << "modulus_remainder of vector\n";
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const VectorReduce *op) {
    internal_assert(op->type.is_scalar()) << "modulus_remainder of vector\n";
    result = ModulusRemainder{};
}

void ComputeModulusRemainder::visit(const LetStmt *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const AssertStmt *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const ProducerConsumer *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const For *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Acquire *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Store *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Provide *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Allocate *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Realize *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Block *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Fork *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Free *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const IfThenElse *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Evaluate *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Prefetch *) {
    internal_error << "modulus_remainder of statement\n";
}

void ComputeModulusRemainder::visit(const Atomic *) {
    internal_error << "modulus_remainder of statement\n";
}

}  // namespace

ModulusRemainder modulus_remainder(const Expr &e) {
    ComputeModulusRemainder mr(nullptr);
    return mr.analyze(e);
}

ModulusRemainder modulus_remainder(const Expr &e, const Scope<ModulusRemainder> &scope) {
    ComputeModulusRemainder mr(&scope);
    return mr.analyze(e);
}

bool reduce_expr_modulo(const Expr &expr, int64_t modulus, int64_t *remainder) {
    ModulusRemainder result = modulus_remainder(expr);

    /* As an example: If we asked for expr mod 8, and the analysis
     * said that expr = 16*k + 13, then because 16 % 8 == 0, the
     * result is 13 % 8 == 5. But if the analysis says that expr =
     * 6*k + 3, then expr mod 8 could be 1, 3, 5, or 7, so we just
     * return false.
     */

    if (mod(result.modulus, modulus) == 0) {
        *remainder = mod(result.remainder, modulus);
        return true;
    } else {
        return false;
    }
}
bool reduce_expr_modulo(const Expr &expr, int64_t modulus, int64_t *remainder, const Scope<ModulusRemainder> &scope) {
    ModulusRemainder result = modulus_remainder(expr, scope);

    if (mod(result.modulus, modulus) == 0) {
        *remainder = mod(result.remainder, modulus);
        return true;
    } else {
        return false;
    }
}

ModulusRemainder ComputeModulusRemainder::analyze(const Expr &e) {
    e.accept(this);
    return result;
}

namespace {
void check(const Expr &e, int64_t m, int64_t r) {
    ModulusRemainder result = modulus_remainder(e);
    if (result.modulus != m || result.remainder != r) {
        std::cerr << "Test failed for modulus_remainder:\n";
        std::cerr << "Expression: " << e << "\n";
        std::cerr << "Correct modulus, remainder  = " << m << ", " << r << "\n";
        std::cerr << "Computed modulus, remainder = "
                  << result.modulus << ", "
                  << result.remainder << "\n";
        exit(1);
    }
}
}  // namespace

void modulus_remainder_test() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    check((30 * x + 3) + (40 * y + 2), 10, 5);
    check((6 * x + 3) * (4 * y + 1), 2, 1);
    check(max(30 * x - 24, 40 * y + 31), 5, 1);
    check(10 * x - 33 * y, 1, 0);
    check(10 * x - 35 * y, 5, 0);
    check(123, 0, 123);
    check(Let::make("y", x * 3 + 4, y * 3 + 4), 9, 7);
    // Check overflow
    check((5045320 * x + 4) * (405713 * y + 3) * (8000123 * x + 4354), 1, 0);

    std::cout << "modulus_remainder test passed\n";
}

int64_t gcd(int64_t a, int64_t b) {
    if (a < b) {
        std::swap(a, b);
    }
    while (b != 0) {
        int64_t tmp = b;
        b = a % b;
        a = tmp;
    }
    return a;
}

int64_t lcm(int64_t a, int64_t b) {
    // Remove all of the common factors from one of the operands
    b /= gcd(a, b);

    // Then multiply. On overflow this will return zero, so ignore the overflow
    // flag.
    int64_t result;
    (void)mul_with_overflow(64, a, b, &result);
    return result;
}

ModulusRemainder operator+(const ModulusRemainder &a, const ModulusRemainder &b) {
    int64_t m = 1, r = 0;
    if (add_with_overflow(64, a.remainder, b.remainder, &r)) {
        m = gcd(a.modulus, b.modulus);
        r = mod(r, m);
    }
    return {m, r};
}

ModulusRemainder operator-(const ModulusRemainder &a, const ModulusRemainder &b) {
    int64_t m = 1, r = 0;
    if (sub_with_overflow(64, a.remainder, b.remainder, &r)) {
        m = gcd(a.modulus, b.modulus);
        r = mod(r, m);
    }
    return {m, r};
}

ModulusRemainder operator*(const ModulusRemainder &a, const ModulusRemainder &b) {
    int64_t m, r;
    if (a.modulus == 0) {
        // a is constant
        if (mul_with_overflow(64, a.remainder, b.modulus, &m) &&
            mul_with_overflow(64, a.remainder, b.remainder, &r)) {
            return {m, r};
        }
    } else if (b.modulus == 0) {
        // b is constant
        if (mul_with_overflow(64, a.modulus, b.remainder, &m) &&
            mul_with_overflow(64, a.remainder, b.remainder, &r)) {
            return {m, r};
        }
    } else if (a.remainder == 0 && b.remainder == 0) {
        // multiple times multiple
        if (mul_with_overflow(64, a.modulus, b.modulus, &m)) {
            return {m, 0};
        }
    } else if (a.remainder == 0) {
        int64_t g = gcd(b.modulus, b.remainder);
        if (mul_with_overflow(64, a.modulus, g, &m)) {
            return {m, 0};
        }
    } else if (b.remainder == 0) {
        int64_t g = gcd(a.modulus, a.remainder);
        if (mul_with_overflow(64, b.modulus, g, &m)) {
            return {m, 0};
        }
    } else {
        // Convert them to the same modulus and multiply
        if (mul_with_overflow(64, a.remainder, b.remainder, &r)) {
            m = gcd(a.modulus, b.modulus);
            r = mod(r, m);
            return {m, r};
        }
    }

    return ModulusRemainder{};
}

ModulusRemainder operator/(const ModulusRemainder &a, const ModulusRemainder &b) {
    // What can we say about:
    // floor((m1 * x + r1) / (m2 * y + r2))

    // If m2 is zero and m1 is a multiple of r2, then we can pull the
    // varying term out of the floor div and the expression simplifies
    // to:
    // (m1 / r2) * x + floor(r1 / r2)
    // E.g. (8x + 3) / 2 -> (4x + 1)

    if (b.modulus == 0 && b.remainder != 0) {
        if (mod(a.modulus, b.remainder) == 0) {
            return {a.modulus / b.remainder, div_imp(a.remainder, b.remainder)};
        }
    }

    return ModulusRemainder{};
}

ModulusRemainder ModulusRemainder::unify(const ModulusRemainder &a, const ModulusRemainder &b) {
    // We don't know if we're going to get a or b, so we'd better find
    // a single modulus remainder that works for both.

    // For example:
    // max(30*_ + 13, 40*_ + 27) ->
    // max(10*_ + 3, 10*_ + 7) ->
    // max(2*_ + 1, 2*_ + 1) ->
    // 2*_ + 1

    if (b.remainder > a.remainder) {
        return unify(b, a);
    }

    // Reduce them to the same modulus and the same remainder
    int64_t modulus = gcd(a.modulus, b.modulus);

    int64_t r;
    if (!sub_with_overflow(64, a.remainder, b.remainder, &r)) {
        // The modulus is not representable as an int64.
        return {0, 1};
    }

    int64_t diff = a.remainder - b.remainder;

    modulus = gcd(diff, modulus);

    int64_t ra = mod(a.remainder, modulus);

    internal_assert(ra == mod(b.remainder, modulus))
        << "There's a bug inside ModulusRemainder in unify_alternatives:\n"
        << "a.modulus         = " << a.modulus << "\n"
        << "a.remainder       = " << a.remainder << "\n"
        << "b.modulus         = " << b.modulus << "\n"
        << "b.remainder       = " << b.remainder << "\n"
        << "diff              = " << diff << "\n"
        << "unified modulus   = " << modulus << "\n"
        << "unified remainder = " << ra << "\n";

    return {modulus, ra};
}

ModulusRemainder ModulusRemainder::intersect(const ModulusRemainder &a, const ModulusRemainder &b) {
    // We have x == ma * y + ra == mb * z + rb

    // We want to synthesize these two facts into one modulus
    // remainder relationship. We are permitted to be
    // conservatively-large, so it's OK if some elements of the result
    // only satisfy one of the two constraints.

    // For coprime ma and mb you want to use the Chinese remainder
    // theorem. In our case, the moduli will almost always be
    // powers of two, so we should just return the smaller of the two
    // sets (usually the one with the larger modulus).
    if (a.modulus == 0) {
        return a;
    }
    if (b.modulus == 0) {
        return b;
    }
    if (a.modulus > b.modulus) {
        return a;
    }
    return b;
}

void ComputeModulusRemainder::visit(const Mod *op) {
    result = analyze(op->a) % analyze(op->b);
}

ModulusRemainder operator%(const ModulusRemainder &a, const ModulusRemainder &b) {
    // For non-zero y, we can treat x mod y as x + z*y, where we know
    // nothing about z.
    // (ax + b) + z (cx + d) ->
    // ax + b + zcx + dz ->
    // gcd(a, c, d) * w + b

    // E.g:
    // (8x + 5) mod (6x + 2) ->
    // (8x + 5) + z (6x + 2) ->
    // (8x + 6zx + 2x) + 5 ->
    // 2(4x + 3zx + x) + 5 ->
    // 2w + 1
    int64_t modulus = gcd(a.modulus, b.modulus);
    modulus = gcd(modulus, b.remainder);
    int64_t remainder = mod(a.remainder, modulus);

    if (b.remainder == 0 && remainder != 0) {
        // b could be zero, so the result could also just be zero.
        if (modulus == 0) {
            remainder = 0;
        } else {
            // This can no longer be expressed as ax + b
            remainder = 0;
            modulus = 1;
        }
    }

    return {modulus, remainder};
}

ModulusRemainder operator+(const ModulusRemainder &a, int64_t b) {
    return a + ModulusRemainder(0, b);
}

ModulusRemainder operator-(const ModulusRemainder &a, int64_t b) {
    return a - ModulusRemainder(0, b);
}

ModulusRemainder operator*(const ModulusRemainder &a, int64_t b) {
    return a * ModulusRemainder(0, b);
}

ModulusRemainder operator/(const ModulusRemainder &a, int64_t b) {
    return a / ModulusRemainder(0, b);
}

ModulusRemainder operator%(const ModulusRemainder &a, int64_t b) {
    return a % ModulusRemainder(0, b);
}

}  // namespace Internal
}  // namespace Halide
