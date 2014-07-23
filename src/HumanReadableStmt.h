#ifndef HALIDE_HUMAN_READABLE_STMT
#define HALIDE_HUMAN_READABLE_STMT 1

/** 
 * returns a statment, simplified given concrete output bounds and other parameters, in order to be as humanly readable as possible
*/

#include "IR.h"
#include "Func.h"
#include "Image.h"
#include "Target.h"

namespace Halide {
namespace Internal {

// please note this function modifies the given Stmt
EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft);
EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements);

}}

#endif
