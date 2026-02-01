#ifndef HALIDE_MODULUS_REMAINDER_H
#define HALIDE_MODULUS_REMAINDER_H

/** \file
 * Routines for statically determining what expressions are divisible by.
 */

#include <cstdint>

#include "Util.h"

namespace Halide {

struct Expr;

namespace Internal {

template<typename T>
class Scope;

/** The result of modulus_remainder analysis. These represent strided
 * subsets of the integers. A ModulusRemainder object m represents all
 * integers x such that there exists y such that x == m.modulus * y +
 * m.remainder. Note that under this definition a set containing a
 * single integer (a constant) is represented using a modulus of
 * zero. These sets can be combined with several mathematical
 * operators in the obvious way. E.g. m1 + m2 contains (at least) all
 * integers x1 + x2 such that x1 belongs to m1 and x2 belongs to
 * m2. These combinations are conservative. If some internal math
 * would overflow, it defaults to all of the integers (modulus == 1,
 * remainder == 0). */

struct ModulusRemainder {
    ModulusRemainder() = default;
    ModulusRemainder(int64_t m, int64_t r)
        : modulus(m), remainder(r) {
    }

    int64_t modulus = 1, remainder = 0;

    // Take a conservatively-large union of two sets. Contains all
    // elements from both sets, and maybe some more stuff.
    static ModulusRemainder unify(const ModulusRemainder &a, const ModulusRemainder &b);

    // Take a conservatively-large intersection. Everything in the
    // result is in at least one of the two sets, but not always both.
    static ModulusRemainder intersect(const ModulusRemainder &a, const ModulusRemainder &b);

    bool operator==(const ModulusRemainder &other) const {
        return (modulus == other.modulus) && (remainder == other.remainder);
    }
};

ModulusRemainder operator+(const ModulusRemainder &a, const ModulusRemainder &b);
ModulusRemainder operator-(const ModulusRemainder &a, const ModulusRemainder &b);
ModulusRemainder operator*(const ModulusRemainder &a, const ModulusRemainder &b);
ModulusRemainder operator/(const ModulusRemainder &a, const ModulusRemainder &b);
ModulusRemainder operator%(const ModulusRemainder &a, const ModulusRemainder &b);

ModulusRemainder operator+(const ModulusRemainder &a, int64_t b);
ModulusRemainder operator-(const ModulusRemainder &a, int64_t b);
ModulusRemainder operator*(const ModulusRemainder &a, int64_t b);
ModulusRemainder operator/(const ModulusRemainder &a, int64_t b);
ModulusRemainder operator%(const ModulusRemainder &a, int64_t b);

/** For things like alignment analysis, often it's helpful to know
 * if an integer expression is some multiple of a constant plus
 * some other constant. For example, it is straight-forward to
 * deduce that ((10*x + 2)*(6*y - 3) - 1) is congruent to five
 * modulo six.
 *
 * We get the most information when the modulus is large. E.g. if
 * something is congruent to 208 modulo 384, then we also know it's
 * congruent to 0 mod 8, and we can possibly use it as an index for an
 * aligned load. If all else fails, we can just say that an integer is
 * congruent to zero modulo one.
 */
ModulusRemainder modulus_remainder(const Expr &e);

/** If we have alignment information about external variables, we can
 * let the analysis know about that using this version of
 * modulus_remainder: */
ModulusRemainder modulus_remainder(const Expr &e, const Scope<ModulusRemainder> &scope);

/** Reduce an expression modulo some integer. Returns true and assigns
 * to remainder if an answer could be found. */
///@{
HALIDE_MUST_USE_RESULT bool reduce_expr_modulo(const Expr &e, int64_t modulus, int64_t *remainder);
HALIDE_MUST_USE_RESULT bool reduce_expr_modulo(const Expr &e, int64_t modulus, int64_t *remainder, const Scope<ModulusRemainder> &scope);
///@}

void modulus_remainder_test();

/** The greatest common divisor of two integers. Returns a positive result,
 * unless both args are INT64_MIN. */
int64_t gcd(int64_t, int64_t);

/** The least common multiple of two integers */
int64_t lcm(int64_t, int64_t);

}  // namespace Internal
}  // namespace Halide

#endif
