#ifndef HALIDE_COPY_ELISION
#define HALIDE_COPY_ELISION

#include <map>

#include "IR.h"

/** \file
 * Define method which return pairs of (consumer, producer) that
 * does simple copies and hence can be elided from the IR.
 */

namespace Halide {
namespace Internal {

/** Return all pairs of functions which operation only involves simple copy
  * of another function and the function from which it copies from.
  * Result: {consumer (store into) -> producer (copy from)}. Ignore the
  * case when consumer has updates or is an extern function. Also, ignore
  * the case when copy elision cannot be safely performed in the IR (i.e
  * producer has multiple consumers or consumer's buffer is allocated after
  * the producer's values are produced).
  */
std::map<std::string, std::string> get_valid_copy_elision_pairs(
	const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
