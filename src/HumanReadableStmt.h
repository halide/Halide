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

EXPORT Stmt human_readable_stmt(Func f,  Realization dst, std::map<std::string, Expr> additional_replacements, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f,  Realization dst, const Target &t = get_jit_target_from_environment());

template<typename T>
EXPORT Stmt human_readable_stmt(Func f, Image<T> dst, std::map<std::string, Expr> additional_replacements, const Target &target = get_jit_target_from_environment());

template<typename T> 
EXPORT Stmt human_readable_stmt(Func f, Image<T> dst, const Target &target = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, int w_size, std::map<std::string, Expr> additional_replacements, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, int w_size, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, std::map<std::string, Expr> additional_replacements, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size,  const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, std::map<std::string, Expr> additional_replacements, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, int y_size, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, std::map<std::string, Expr> additional_replacements, const Target &t = get_jit_target_from_environment());

EXPORT Stmt human_readable_stmt(Func f, int x_size, const Target &t = get_jit_target_from_environment());

}}

#endif
