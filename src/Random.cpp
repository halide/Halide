#include "Random.h"
#include "Func.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// Some randomly-generated integers.
#define C0 576942909
#define C1 1121052041
#define C2 1040796640

// Permute a 32-bit unsigned integer using a fixed psuedorandom
// permutation.
Expr rng32(const Expr &x) {
    internal_assert(x.type() == UInt(32));

    // A polynomial P with coefficients C0 .. CN induces a permutation
    // modulo 2^d iff:
    // 1) P(0) != P(1) modulo 2
    // 2) sum(i * Ci) is odd

    // (See http://en.wikipedia.org/wiki/Permutation_polynomial#Rings_Z.2FpkZ)

    // For a quadratic, this is only satisfied by:
    // C0 anything
    // C1 odd
    // C2 even

    // The coefficients defined above were chosen to satisfy this
    // property.

    // It's pretty random, but note that the quadratic term disappears
    // if inputs are the multiples of 2^16, and so you get a linear
    // sequence.  However, *that* linear sequence probably varies in
    // the low bits, so if you run in through the permutation again,
    // you should break it up. All actual use of this runs it through
    // multiple times in order to combine several inputs, so it should
    // be ok. The other flaw is it's a permutation, so you get no
    // collisions. Birthday paradox be damned.

    // However, it's exceedingly cheap to compute, as it only uses
    // vectorizable int32 muls and adds, and the resulting numbers:
    // - Have the correct moments for a uniform distribution
    // - Have no serial correlations in any of the bits
    // - Have a completely flat power spectrum
    // - Have no visible patterns

    // So I declare this good enough for image processing.

    // If it's just a const (which it often is), save the simplifier some work:
    if (const uint64_t *i = as_const_uint(x)) {
        return make_const(UInt(32), ((C2 * (*i)) + C1) * (*i) + C0);
    }

    return (((C2 * x) + C1) * x) + C0;
}
}  // namespace

Expr random_int(const vector<Expr> &e) {
    internal_assert(!e.empty());
    internal_assert(e[0].type() == Int(32) || e[0].type() == UInt(32));
    // Permute the first term
    Expr result = rng32(cast(UInt(32), e[0]));
    for (size_t i = 1; i < e.size(); i++) {
        internal_assert(e[i].type() == Int(32) || e[i].type() == UInt(32));
        // Add in the next term and permute again
        string name = unique_name('R');
        // If it's a const, save the simplifier some work
        const uint64_t *ir = as_const_uint(result);
        const uint64_t *ie = as_const_uint(e[i]);
        if (ir && ie) {
            result = rng32(make_const(UInt(32), (*ir) + (*ie)));
        } else {
            result = Let::make(name, result + cast<uint32_t>(e[i]),
                               rng32(Variable::make(UInt(32), name)));
        }
    }
    // The low bytes of this have a poor period, so mix in the high bytes for
    // two additional instructions.
    result = result ^ (result >> 16);

    return result;
}

Expr random_float(const vector<Expr> &e) {
    Expr result = random_int(e);
    // Set the exponent to one, and fill the mantissa with 23 random bits.
    result = (127 << 23) | (cast<uint32_t>(result) >> 9);
    // The clamp is purely for the benefit of bounds inference.
    return clamp(reinterpret(Float(32), result) - 1.0f, 0.0f, 1.0f);
}

namespace {

class LowerRandom : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::random)) {
            vector<Expr> args = op->args;
            // Insert the free vars in reverse, so innermost vars typically end
            // up last.
            args.insert(args.end(), extra_args.rbegin(), extra_args.rend());
            if (op->type == Float(32)) {
                return random_float(args);
            } else if (op->type == Int(32)) {
                return cast<int32_t>(random_int(args));
            } else if (op->type == UInt(32)) {
                return random_int(args);
            } else {
                internal_error << "The intrinsic random() returns an Int(32), UInt(32) or a Float(32).\n";
                return Expr();
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    vector<Expr> extra_args;

public:
    LowerRandom(const vector<VarOrRVar> &free_vars, int tag) {
        for (const VarOrRVar &v : free_vars) {
            if (v.is_rvar) {
                extra_args.push_back(v.rvar);
            } else {
                extra_args.push_back(v.var);
            }
        }
        extra_args.emplace_back(tag);
    }
};

}  // namespace

Expr lower_random(const Expr &e, const vector<VarOrRVar> &free_vars, int tag) {
    LowerRandom r(free_vars, tag);
    return r.mutate(e);
}

}  // namespace Internal
}  // namespace Halide
