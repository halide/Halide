#include "FreeVariables.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

void FreeVariables::include(const Expr &e) {
    e.accept(this);
}

void FreeVariables::include(const Stmt &s) {
    s.accept(this);
}

void FreeVariables::visit(const Variable *op) {
    if (!scope.contains(op->name)) {
        vars[op->name] = op->type;
    }
}

template<typename LetOrLetStmt>
void FreeVariables::visit_let(const LetOrLetStmt *op) {
    vector<ScopedBinding<>> frame;
    decltype(op->body) body;
    do {
        op->value.accept(this);
        frame.emplace_back(scope, op->name);
        body = op->body;
        op = body.template as<LetOrLetStmt>();
    } while (op);
    body.accept(this);
}

void FreeVariables::visit(const Let *op) {
    visit_let(op);
}

void FreeVariables::visit(const LetStmt *op) {
    visit_let(op);
}

void FreeVariables::visit(const For *op) {
    op->min.accept(this);
    op->max.accept(this);
    ScopedBinding<> bind(scope, op->name);
    op->body.accept(this);
}

std::map<string, Type> find_free_vars(const Expr &e) {
    FreeVariables finder;
    finder.include(e);
    return finder.vars;
}

std::map<string, Type> find_free_vars(const Stmt &s) {
    FreeVariables finder;
    finder.include(s);
    return finder.vars;
}

UnboundVarChecker::UnboundVarChecker(bool check_func_calls)
    : check_func_calls(check_func_calls) {
}

void UnboundVarChecker::visit(const Variable *op) {
    if (!op->param.defined() && !op->image.defined() && !scope.contains(op->name)) {
        offending_var = op->name;
    }
}

void UnboundVarChecker::visit(const Let *op) {
    ScopedBinding<> bind(scope, op->name);
    IRGraphVisitor::visit(op);
}

void UnboundVarChecker::visit(const Call *op) {
    IRGraphVisitor::visit(op);
    if (check_func_calls && op->call_type == Call::Halide) {
        offending_func = op->name;
    }
}

}  // namespace Internal
}  // namespace Halide
