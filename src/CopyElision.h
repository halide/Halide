#ifndef HALIDE_COPY_ELISION
#define HALIDE_COPY_ELISION

#include <map>

#include "IR.h"

/** \file
 * Define method which return pairs of (consumer, producer) that
 * does simple copies and is safe to be elided from the IR.
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
  *
  * If we have copy-elision pair chains, this will also simplify them into
  * {last consumer -> initial producer}. For example, if we have the following
  * case: {{"out" -> "g"}, {"g" -> "f"}}, this will simplify it into the
  * following: {{"out" -> "f"}, {"g" -> ""}}. Note that, {"g" -> ""} is still
  * kept in the list since the producer of "g" is no longer needed and needs
  * to be eliminated from the IR (the empty producer is basically a hint
  * for \ref schedule_functions to do that).
  */
std::map<std::string, std::string> get_valid_copy_elision_pairs(
	const std::vector<Function> &outputs,
	const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
