#include "Random.h"
#include "IROperator.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

namespace {

// Some randomly-generated integers.
#define C0 576942909
#define C1 1121052041
#define C2 1040796640

// Permute a 32-bit unsigned integer using a fixed psuedorandom
// permutation.
Expr rng32(Expr x) {
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

}

Expr random_int(const vector<Expr> &e) {
    internal_assert(e.size());
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
    return result;
}

Expr random_float(const vector<Expr> &e) {
    Expr result = random_int(e);
    // Set the exponent to one, and fill the mantissa with 23 random bits.
    result = (127 << 23) | (cast<uint32_t>(result) >> 9);
    // The clamp is purely for the benefit of bounds inference.
    return clamp(reinterpret(Float(32), result) - 1.0f, 0.0f, 1.0f);
}

class LowerRandom : public IRMutator {
    using IRMutator::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::random)) {
            vector<Expr> args = op->args;
            args.insert(args.end(), extra_args.begin(), extra_args.end());
            if (op->type == Float(32)) {
                expr = random_float(args);
            } else if (op->type == Int(32)) {
                expr = cast<int32_t>(random_int(args));
            } else if (op->type == UInt(32)) {
                expr = random_int(args);
            } else {
                internal_error << "The intrinsic random() returns an Int(32), UInt(32) or a Float(32).\n";
            }
        } else {
            IRMutator::visit(op);
        }
    }

    vector<Expr> extra_args;
public:
    LowerRandom(const vector<string> &free_vars, int tag) {
        extra_args.push_back(tag);
        for (size_t i = 0; i < free_vars.size(); i++) {
            internal_assert(!free_vars[i].empty());
            extra_args.push_back(Variable::make(Int(32), free_vars[i]));
        }
    }
};

Expr lower_random(Expr e, const vector<string> &free_vars, int tag) {
    LowerRandom r(free_vars, tag);
    return r.mutate(e);
}

}
}
