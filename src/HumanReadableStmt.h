#ifndef HALIDE_HUMAN_READABLE_STMT
#define HALIDE_HUMAN_READABLE_STMT

/** \file
* Defines methods for simplifying a stmt into a human-readable form.
*/

#include <map>

#include "IR.h"
#include "Function.h"
#include "Buffer.h"

namespace Halide {
namespace Internal {

/**
 * Returns a Stmt simplified using a concrete size of the output, and
 * other optional values for parameters.
 */
// @{
EXPORT Stmt human_readable_stmt(Function f, Stmt s, Buffer buf);
EXPORT Stmt human_readable_stmt(Function f, Stmt s, Buffer buf,
                                std::map<std::string, Expr> additional_replacements);
// @}

}}

#endif
