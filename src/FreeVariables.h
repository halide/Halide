#ifndef HALIDE_FREE_VARIABLES_H
#define HALIDE_FREE_VARIABLES_H

/** \file
 * Defines a visitor that collects the free variables of a piece of IR. */

#include <map>
#include <string>

#include "Expr.h"
#include "IRVisitor.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Collects every Variable reference in an Expr/Stmt that isn't bound by an
 * enclosing Let, LetStmt, or For within the visited subtree(s), mapped to
 * its type. Variables carrying a Parameter or Buffer are included just
 * like any other free reference.
 *
 * include() may be called more than once (on more than one Expr/Stmt) to
 * accumulate the free variables of several pieces of IR into one result,
 * e.g. an Expr and then the values of the Lets it may later be wrapped
 * in. */
class FreeVariables : public IRVisitor {
public:
    std::map<std::string, Type> vars;

    void include(const Expr &e);
    void include(const Stmt &s);

protected:
    using IRVisitor::visit;
    void visit(const Variable *op) override;
    void visit(const Let *op) override;
    void visit(const LetStmt *op) override;
    void visit(const For *op) override;

private:
    Scope<> scope;

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op);
};

/** Convenience wrapper for the common case of finding the free variables of
 * a single Expr or Stmt. */
// @{
std::map<std::string, Type> find_free_vars(const Expr &e);
std::map<std::string, Type> find_free_vars(const Stmt &s);
// @}

/** Checks whether a subtree references any Var/RVar not bound by an
 * enclosing Let (recorded in offending_var, if any), and, if
 * check_func_calls is set, whether it calls any Halide Func (recorded in
 * offending_func, if any). A Variable carrying a Parameter or Buffer is
 * not considered offending. Correctly respects Let-shadowing via a Scope.
 *
 * This is graph-aware (IRGraphVisitor) rather than tree-based, because the
 * Exprs it's used to validate are raw, user-authored conditions that may
 * share subexpressions and haven't been through CSE.
 *
 * Used to validate conditions passed to Stage::specialize,
 * Pipeline::add_requirement, and the region bounds of an RDom -- none of
 * which may depend on a Var or RVar. */
class UnboundVarChecker : public IRGraphVisitor {
public:
    std::string offending_var;
    std::string offending_func;

    explicit UnboundVarChecker(bool check_func_calls = false);

protected:
    using IRGraphVisitor::visit;
    void visit(const Variable *op) override;
    void visit(const Let *op) override;
    void visit(const Call *op) override;

private:
    bool check_func_calls;
    Scope<> scope;
};

}  // namespace Internal
}  // namespace Halide

#endif
