#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "LinearSolve.h"
#include "Var.h"

namespace Halide {
namespace Internal {

class ExprLinearity : public IRVisitor {
public:
    const Scope<int> &free_vars;
    int result;

    ExprLinearity(const Scope<int> &fv, const Scope<int> *bv = NULL) :
            free_vars(fv), result(0) {
        bound_vars.set_containing_scope(bv);
    }
private:
    using IRVisitor::visit;

    Scope<int> bound_vars;

    void visit(const IntImm *op) {
        result = Linearity::Constant;
    }

    void visit(const FloatImm *op) {
        result = Linearity::Constant;
    }

    // These nodes are considered to introduce non-linearities.
    void visit(const Mod *op) {result = Linearity::NonLinear;}
    void visit(const Min *op) {result = Linearity::NonLinear;}
    void visit(const Max *op) {result = Linearity::NonLinear;}
    void visit(const Select *op) {result = Linearity::NonLinear;}
    void visit(const Call *op) {result = Linearity::NonLinear;}
    void visit(const Load *op) {result = Linearity::NonLinear;}
    void visit(const Ramp *op) {result = Linearity::NonLinear;}
    void visit(const Broadcast *op) {result = Linearity::NonLinear;}

    template<class Op>
    void visit_binary_op(const Op *op) {
        op->a.accept(this);
        int result_a = result;

        result = 0;
        op->b.accept(this);
        int result_b = result;

        if (Linearity::is_nonlinear(result_a) || Linearity::is_nonlinear(result_b)) {
            result = Linearity::NonLinear;
        } else if (Linearity::is_constant(result_a) && Linearity::is_constant(result_b)) {
            result = Linearity::Constant;
        } else {
            result = Linearity::Linear;
        }
    }

    void visit(const Add *op) {visit_binary_op(op);}
    void visit(const Sub *op) {visit_binary_op(op);}
    void visit(const EQ *op)  {visit_binary_op(op);}
    void visit(const NE *op)  {visit_binary_op(op);}
    void visit(const LT *op)  {visit_binary_op(op);}
    void visit(const GT *op)  {visit_binary_op(op);}
    void visit(const LE *op)  {visit_binary_op(op);}
    void visit(const GE *op)  {visit_binary_op(op);}
    void visit(const And *op) {visit_binary_op(op);}
    void visit(const Or *op)  {visit_binary_op(op);}

    void visit(const Div *op) {
        // Integer division is considered non-linear.
        if (op->type.is_int() || op->type.is_uint()) {
            result = false;
        } else {
            op->a.accept(this);
            int result_a = result;

            result = 0;
            op->b.accept(this);
            int result_b = result;

            if (!Linearity::is_constant(result_b)) {
                result = Linearity::NonLinear;
            } else if (Linearity::is_linear(result_a)) {
                result = Linearity::Linear;
            }
        }
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        int result_a = result;

        result = 0;
        op->b.accept(this);
        int result_b = result;

        result = result_a + result_b;
    }

    void visit(const Let *op) {
        int old_result = result;
        op->value.accept(this);
        bound_vars.push(op->name, result);
        result = old_result;
        op->body.accept(this);
        bound_vars.pop(op->name);
    }

    void visit(const Variable *op) {
        if (free_vars.contains(op->name)) {
            result = Linearity::Linear;
        } else if (bound_vars.contains(op->name)) {
            result = bound_vars.get(op->name);
        } else {
            result = Linearity::Constant;
        }
    }
};

int expr_linearity(Expr expr, const std::string &var) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    ExprLinearity linearity(free_vars);
    expr.accept(&linearity);
    return linearity.result;
}

int expr_linearity(Expr expr, const std::string &var, const Scope<int> &bound_vars) {
    Scope<int> free_vars;
    free_vars.push(var, 0);

    ExprLinearity linearity(free_vars, &bound_vars);
    expr.accept(&linearity);
    return linearity.result;
}

int expr_linearity(Expr expr, const Scope<int> &free_vars) {
    ExprLinearity linearity(free_vars);
    expr.accept(&linearity);
    return linearity.result;
}

int expr_linearity(Expr expr, const Scope<int> &free_vars, const Scope<int> &bound_vars) {
    ExprLinearity linearity(free_vars, &bound_vars);
    expr.accept(&linearity);
    return linearity.result;
}

bool expr_is_linear_in_var(Expr expr, const std::string &var) {
    return Linearity::is_linear(expr_linearity(expr, var));
}

bool expr_is_linear_in_var(Expr expr, const std::string &var, const Scope<int> &bound_vars) {
    return Linearity::is_linear(expr_linearity(expr, var, bound_vars));
}

bool expr_is_linear_in_vars(Expr expr, const Scope<int> &free_vars) {
    return Linearity::is_linear(expr_linearity(expr, free_vars));
}

bool expr_is_linear_in_vars(Expr expr, const Scope<int> &free_vars, const Scope<int> &bound_vars) {
    return Linearity::is_linear(expr_linearity(expr, free_vars, bound_vars));
}

class CollectLinearTerms : public IRVisitor {
public:
    const Scope<int> &free_vars;
    Scope<Expr> scope;
    std::vector<Term> terms;
    bool success;

    CollectLinearTerms(const Scope<int> &vars, const Scope<Expr> *s = NULL) :
            free_vars(vars), success(true) {
        terms.resize(1);
        terms[0].var = NULL;
        coeff.push(1);
        scope.set_containing_scope(s);
    }
private:
    SmallStack<Expr> coeff;

    bool has_vars(Expr expr) {
        return expr_uses_vars(expr, free_vars, scope);
    }

    void add_to_constant_term(Expr e) {
        internal_assert(!e.type().is_uint()) << "cannot perform solve with uint types.\n";

        if (terms[0].coeff.defined()) {
            terms[0].coeff = simplify(terms[0].coeff + (coeff.top() * e));
        } else {
            terms[0].coeff = simplify(coeff.top() * e);
        }
    }

    using IRVisitor::visit;

    void visit(const IntImm *op) {
        add_to_constant_term(op);
    }

    void visit(const FloatImm *op) {
        add_to_constant_term(op);
    }

    /* We don't deal with these nodes. */
    void visit(const Mod *op) {success = false;}
    void visit(const Min *op) {success = false;}
    void visit(const Max *op) {success = false;}
    void visit(const EQ *op) {success = false;}
    void visit(const NE *op) {success = false;}
    void visit(const LT *op) {success = false;}
    void visit(const GT *op) {success = false;}
    void visit(const LE *op) {success = false;}
    void visit(const GE *op) {success = false;}
    void visit(const And *op) {success = false;}
    void visit(const Or *op) {success = false;}
    void visit(const Not *op) {success = false;}
    void visit(const Select *op) {success = false;}
    void visit(const Call *op) {success = false;}
    void visit(const Load *op) {success = false;}
    void visit(const Ramp *op) {success = false;}
    void visit(const Broadcast *op) {success = false;}

    void visit(const Add *op) {
        if (has_vars(op->a)) {
            op->a.accept(this);
        } else {
            add_to_constant_term(op->a);
        }

        if (has_vars(op->b)) {
            op->b.accept(this);
        } else {
            add_to_constant_term(op->b);
        }
    }

    void visit(const Sub *op) {
        if (has_vars(op->a)) {
            op->a.accept(this);
        } else {
            add_to_constant_term(op->a);
        }

        coeff.push(-coeff.top_ref());
        if (has_vars(op->b)) {
            op->b.accept(this);
        } else {
            add_to_constant_term(op->b);
        }
        coeff.pop();
    }

    void visit(const Div *op) {
        // We don't simplify across integer division.
        if (op->type.is_int() || op->type.is_uint()) {
            success = false;
            return;
        }

        Expr a = op->a;
        Expr b = op->b;

        if (has_vars(b)) {
            success = false;
        } else if (has_vars(a)) {
            internal_assert(!b.type().is_uint()) << "cannot perform solve with uint types.\n";

            coeff.push(coeff.top() / b);
            a.accept(this);
            coeff.pop();
        } else {
            add_to_constant_term(op);
        }
    }

    void visit(const Mul *op) {
        Expr a = op->a;
        Expr b = op->b;

        bool a_has_var = has_vars(a);
        bool b_has_var = has_vars(b);

        if (a_has_var && b_has_var) {
            success = false;
        } else if (a_has_var) {
            internal_assert(!b.type().is_uint()) << "cannot perform solve with uint types.\n";

            coeff.push(coeff.top() * b);
            a.accept(this);
            coeff.pop();
        } else if (b_has_var) {
            internal_assert(!a.type().is_uint()) << "cannot perform solve with uint types.\n";

            coeff.push(coeff.top() * a);
            b.accept(this);
            coeff.pop();
        } else {
            add_to_constant_term(op);
        }
    }

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            scope.get(op->name).accept(this);
        } else {
            Term t = {simplify(coeff.top()), op};
            terms.push_back(t);
        }
    }
};

void collect_terms(std::vector<Term> &old_terms, std::vector<Term> &new_terms) {
    std::map<std::string, int> term_map;

    Term t0 = {make_zero(Int(32)), NULL};
    new_terms.push_back(t0);
    for (size_t i = 0; i < old_terms.size(); ++i) {
        if (!old_terms[i].coeff.defined())
            continue;

        if (old_terms[i].var) {
            const std::string& var_name = old_terms[i].var->name;
            if (term_map.count(var_name)) {
                Term& t = new_terms[term_map[var_name]];
                t.coeff = simplify(Add::make(t.coeff, old_terms[i].coeff));
            } else {
                term_map[var_name] = new_terms.size();
                new_terms.push_back(old_terms[i]);
            }
        } else {
            new_terms[0].coeff = simplify(new_terms[0].coeff + old_terms[i].coeff);
        }
    }
}

Expr linear_expr(const std::vector<Term> &terms) {
    Expr expr;

    if (terms[0].var) {
        expr = terms[0].coeff * terms[0].var;
    } else {
        expr = terms[0].coeff;
    }

    for (size_t i = 1; i < terms.size(); ++i) {
        if (terms[i].var) {
            expr += terms[i].coeff * terms[i].var;
        } else {
            expr += terms[i].coeff;
        }
    }

    return simplify(expr);
}

bool collect_linear_terms(Expr e, std::vector<Term> &terms, const Scope<int> &free_vars) {
    CollectLinearTerms linear_terms(free_vars);
    e.accept(&linear_terms);
    if (linear_terms.success) {
        collect_terms(linear_terms.terms, terms);
        return true;
    } else {
        return false;
    }
}

bool collect_linear_terms(Expr e, std::vector<Term> &terms,
                          const Scope<int> &free_vars,
                          const Scope<Expr> &scope) {
    CollectLinearTerms linear_terms(free_vars, &scope);
    e.accept(&linear_terms);
    if (linear_terms.success) {
        collect_terms(linear_terms.terms, terms);
        return true;
    } else {
        return false;
    }
}


class SolveForLinearVariable : public IRMutator {
public:
    const Scope<int> &free_vars;
    std::string var_name;
    Scope<Expr> scope;
    bool solved;

    SolveForLinearVariable(const std::string& var, const Scope<int> &vars, const Scope<Expr> *s = NULL) :
            free_vars(vars), var_name(var), solved(false) {
        scope.set_containing_scope(s);
    }

private:
    using IRVisitor::visit;

    int find_var(const std::vector<Term>& terms) {
        for (size_t i = 0; i < terms.size(); ++i) {
            if (terms[i].var && terms[i].var->name == var_name) {
                return i;
            }
        }
        return -1;
    }

    /* We don't deal with these nodes. */
    void visit(const Add *op) {solved = false;}
    void visit(const Sub *op) {solved = false;}
    void visit(const Mul *op) {solved = false;}
    void visit(const Div *op) {solved = false;}
    void visit(const Mod *op) {solved = false;}
    void visit(const Min *op) {solved = false;}
    void visit(const Max *op) {solved = false;}
    void visit(const Select *op) {solved = false;}
    void visit(const Call *op) {solved = false;}
    void visit(const Load *op) {solved = false;}
    void visit(const Ramp *op) {solved = false;}
    void visit(const Broadcast *op) {solved = false;}

    void visit(const Cast *op) {
        Expr value = mutate(op->value);
        if (!value.defined()) {
            expr = Expr();
        } else if (value.same_as(op->value)) {
            expr = op;
        } else {
            expr = Cast::make(op->type, value);
        }
    }

    template<class Cmp, class Op>
    void visit_compare(const Cmp *op, bool is_equality, bool is_less, bool is_open) {
        Expr lhs = op->a;
        Expr rhs = op->b;

        std::vector<Term> lhs_terms;
        std::vector<Term> rhs_terms;

        bool lhs_is_linear = collect_linear_terms(lhs, lhs_terms, free_vars, scope);
        bool rhs_is_linear = collect_linear_terms(rhs, rhs_terms, free_vars, scope);

        if (lhs_is_linear && rhs_is_linear) {
            int lhs_var = find_var(lhs_terms);
            int rhs_var = find_var(rhs_terms);

            bool swapped = false;

            if (rhs_var > -1) {
                if (lhs_var > -1) {
                    // First, move the instance of loop var from RHS to LHS.
                    Term t1 = lhs_terms[lhs_var];
                    Term t2 = rhs_terms[rhs_var];
                    lhs_terms[lhs_var].coeff = simplify(t1.coeff - t2.coeff);
                    rhs_terms[rhs_var] = rhs_terms.back();
                    rhs_terms.pop_back();
                } else {
                    std::swap(lhs, rhs);
                    std::swap(lhs_var, rhs_var);
                    std::swap(lhs_terms, rhs_terms);
                    swapped = true;
                }
            }

            if (lhs_var > -1) {
                // At this point we know that the variable we want
                // only appears on the left hand side.
                for (size_t i = 0; i < lhs_terms.size(); ++i) {
                    if ((int)i != lhs_var) {
                        Term t = lhs_terms[i];
                        t.coeff = simplify(-t.coeff);
                        rhs_terms.push_back(t);
                    }
                }

                Term var_term = lhs_terms[lhs_var];
                lhs_terms.clear();
                collect_terms(rhs_terms, lhs_terms);
                rhs_terms.swap(lhs_terms);

                rhs = linear_expr(rhs_terms);
                if (is_negative_const(var_term.coeff)) {
                    var_term.coeff = simplify(-var_term.coeff);
                    rhs = simplify(-rhs);
                    swapped = !swapped;
                }

                if (is_zero(var_term.coeff)) {
                    rhs = simplify(Cast::make(var_term.var->type, rhs));
                    lhs = make_zero(var_term.var->type);
                } else {
                    if (var_term.coeff.type().is_int() && rhs.type().is_int()) {
                        if (is_equality) {
                            // If we are dealing with integer types in an equality equation, then we
                            // don't divide by the coefficient in the solver.
                            lhs = simplify(var_term.coeff * var_term.var);
                        } else if ((is_less != swapped && is_open) ||
                                   (is_less == swapped && !is_open)) {
                            // If we are solving an integer < or a >= comparison than we must use the
                            // ceiling of the division as the respective bound.
                            rhs = (rhs + var_term.coeff - 1) / var_term.coeff;
                            lhs = var_term.var;
                        } else {
                            rhs = rhs / var_term.coeff;
                            lhs = var_term.var;
                        }
                    } else {
                        rhs = rhs / var_term.coeff;
                        lhs = var_term.var;
                    }

                    rhs = simplify(Cast::make(var_term.var->type, rhs));
                    solved = true;
                }
            }

            if (swapped) {
                expr = Op::make(lhs, rhs);
            } else {
                expr = Cmp::make(lhs, rhs);
            }
        } else {
            expr = op;
        }
    }

    void visit(const EQ *op) {visit_compare<EQ, EQ>(op, true,  false, false);}
    void visit(const NE *op) {visit_compare<NE, NE>(op, true,  false, false);}
    void visit(const LT *op) {visit_compare<LT, GT>(op, false, true,  true);}
    void visit(const LE *op) {visit_compare<LE, GE>(op, false, true,  false);}
    void visit(const GT *op) {visit_compare<GT, LT>(op, false, false, true);}
    void visit(const GE *op) {visit_compare<GE, LE>(op, false, false, false);}

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        mutate(op->body);
        scope.pop(op->name);
    }
};

Expr solve_for_linear_variable(Expr e, Var x, const Scope<int> &free_vars) {
    SolveForLinearVariable solver(x.name(), free_vars);
    Expr s = solver.mutate(e);
    if (solver.solved) {
        return s;
    } else {
        return e;
    }
}

Expr solve_for_linear_variable(Expr e, Var x, const Scope<int> &free_vars, const Scope<Expr> &scope) {
    SolveForLinearVariable solver(x.name(), free_vars, &scope);
    Expr s = solver.mutate(e);
    if (solver.solved) {
        return s;
    } else {
        return e;
    }
}

}
}
