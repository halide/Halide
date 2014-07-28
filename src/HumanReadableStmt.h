#ifndef HALIDE_HUMAN_READABLE_STMT
#define HALIDE_HUMAN_READABLE_STMT 

/** \file 
* Defines a function which applies subsitutions based on the given inputs.
*/

#include "IR.h"
#include "Func.h"
#include "Image.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/**
* Returns a Stmt with the substitiutions deduced by buffer_t and optionally
* defined in string to Expr map.*/
// @{
EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft);
EXPORT Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements);
// @}

}}

#endif
