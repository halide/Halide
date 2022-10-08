#ifndef HL_ROSETTE_H
#define HL_ROSETTE_H

#include "Expr.h"
#include "Bounds.h"
#include <functional>
#include <map>
#include <set>
#include <string>

namespace Halide {

namespace Internal {

void generate_arm_interpreter();

enum VarEncoding { Bitvector, Integer };

std::map<std::string, VarEncoding> get_encoding(const Expr &expr, const std::map<std::string, Expr> &let_vars, const std::map<std::string, Expr> &llet_vars);
std::string expr_to_racket(const Expr &expr, int indent = 1);
std::string expr_to_racket(const Expr &expr, const std::map<std::string, VarEncoding> &encoding, const std::map<std::string, Expr> &let_vars, int indent = 1);
std::function<std::string(const Expr &, bool, bool)> get_expr_racket_dispatch(const Expr &expr, const std::map<std::string, VarEncoding> &encoding, const std::map<std::string, Expr> &let_vars);
std::string type_to_rake_type(Type type, bool include_space, bool c_plus_plus);

Stmt rake_optimize_hvx(const FuncValueBounds &fvb, const Stmt &s, std::set<const BaseExprNode *> &mutated_exprs, const std::map<std::string, Interval> &bounds);

// Stmt rake_optimize_arm(FuncValueBounds fvb, const Stmt &s, std::set<const BaseExprNode *> &mutated_exprs, const std::map<std::string, Interval> &bounds);

}  // namespace Internal

}  // namespace Halide

#endif  // HL_ROSETTE_H
