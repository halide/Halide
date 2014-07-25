#ifndef HALIDE_HUMAN_READABLE_STMT
#define HALIDE_HUMAN_READABLE_STMT 

/** 
 * This class returns an statement with the bounds, any anyother given argument
 * substituted and simpliefied. 
*/

#include "IR.h"
#include "Func.h"
#include "Image.h"
#include "Target.h"

namespace Halide {
namespace Internal {

EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft);
EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements);

}}

#endif
