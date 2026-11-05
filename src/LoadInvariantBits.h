#ifndef HALIDE_LOAD_INVARIANT_BITS_H
#define HALIDE_LOAD_INVARIANT_BITS_H

/** \file
 * Defines an analysis of which bits of a variable leave a definition's loads
 * in place.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

class Definition;

/** For each load in the values of the definition d, the mask of the bits of
 * the variable var (a pure Var or an RVar of d) that leave it in place: bit b
 * is set if, for every value of var in [0, 2^bits), flipping bit b of var
 * leaves all of the load's arguments provably equal. The other variables stay
 * symbolic. A load is a call to a Buffer, an ImageParam or a Func that is not
 * inlined here (calls to Funcs that can be inlined are inlined first, then
 * lets substituted and promises and likelies removed); for an element of a
 * struct, its arguments are the struct load's, then the field's index and the
 * element's. Loads that do not depend on var have every bit set. The masks are
 * in no particular order, one per occurrence of a load. For example, if d is
 * f() += cast<int>((qs(r / 2) >> (r % 2 * 4)) & 15) * a(r), with r over
 * [0, 32), then with bits = 5 the masks are 31 (f() itself), 1 (the two
 * nibbles of a byte of qs share its load) and 0 (a(r)).
 *
 * The analysis enumerates the 2^bits values of var, so bits should be
 * small. It creates no unique names, so calling it does not change the
 * names (or the code) of anything lowered afterwards. */
std::vector<uint64_t> load_invariant_bits(const Definition &d, const std::string &var, int bits);

}  // namespace Internal
}  // namespace Halide

#endif
