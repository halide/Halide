#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "LinearSolve.h"
#include "Var.h"

namespace Halide {
namespace Internal {

namespace {

class HasVariable : public IRVisitor {
  public:
    bool has_var;

    HasVariable() : has_var(false) {}

  private:
    using IRVisitor::visit;

    void visit(const Variable* op) {
        has_var = true;
    }
};

}

class CollectLinearTerms : public IRVisitor {
public:
    Scope<Expr> scope;
    std::vector<Term> terms;
    bool success;

    CollectLinearTerms(const Scope<Expr> *s = NULL) : success(true) {
        terms.resize(1);
        terms[0].var = NULL;

        coeff.push(make_one(Int(32)));

        scope.set_containing_scope(s);
    }
private:
    SmallStack<Expr> coeff;

    bool has_var(Expr e) {
        HasVariable check;
        e.accept(&check);
        return check.has_var;
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
    void visit(const Load *op) {success = false;}
    void visit(const Ramp *op) {success = false;}
    void visit(const Broadcast *op) {success = false;}

    void visit(const Add *op) {
        if (has_var(op->a)) {
            op->a.accept(this);
        } else {
            add_to_constant_term(op->a);
        }

        if (has_var(op->b)) {
            op->b.accept(this);
        } else {
            add_to_constant_term(op->b);
        }
    }

    void visit(const Sub *op) {
        if (has_var(op->a)) {
            op->a.accept(this);
        } else {
            add_to_constant_term(op->a);
        }

        coeff.push(-coeff.top_ref());
        if (has_var(op->b)) {
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

        if (has_var(b)) {
            success = false;
        } else if (has_var(a)) {
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

        bool a_has_var = has_var(a);
        bool b_has_var = has_var(b);

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

void collect_terms(std::vector<Term>& old_terms, std::vector<Term>& new_terms) {
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
            match_types(new_terms[0].coeff, old_terms[i].coeff);
            new_terms[0].coeff = simplify(Add::make(new_terms[0].coeff, old_terms[i].coeff));
        }
    }
}

Expr linear_expr(const std::vector<Term>& terms) {
    Expr expr;

    if (terms[0].var) {
        expr = Mul::make(terms[0].coeff, terms[0].var);
    } else {
        expr = terms[0].coeff;
    }

    for (size_t i = 1; i < terms.size(); ++i) {
        if (terms[i].var) {
            Expr c = terms[i].coeff;
            Expr var = terms[i].var;
            match_types(c, var);

            Expr prod = Mul::make(c, var);
            match_types(prod, expr);
            expr = Add::make(expr, prod);
        } else {
            Expr c = terms[i].coeff;
            match_types(c, expr);
            expr = Add::make(expr, c);
        }
    }

    return simplify(expr);
}

bool collect_linear_terms(Expr e, std::vector<Term>& terms) {
    CollectLinearTerms linear_terms;
    e.accept(&linear_terms);
    if (linear_terms.success) {
        collect_terms(linear_terms.terms, terms);
        return true;
    } else {
        return false;
    }
}

bool collect_linear_terms(Expr e, std::vector<Term>& terms, const Scope<Expr> &scope) {
    CollectLinearTerms linear_terms(&scope);
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
    std::string  var_name;
    Scope<Expr>  scope;
    bool solved;

    SolveForLinearVariable(const std::string& var, const Scope<Expr> *s = NULL) :
            var_name(var), solved(false) {
        scope.set_containing_scope(s);
    }
private:
    using IRVisitor::visit;

    int find_var(const std::vector<Term>& terms) {
        for (size_t i = 0; i < terms.size(); ++i) {
            if (terms[i].var && terms[i].var->name == var_name)
                return i;
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

    template<class Cmp>
    void visit_sym_cmp(const Cmp *op) {
        Expr lhs = op->a;
        Expr rhs = op->b;

        std::vector<Term> lhs_terms;
        std::vector<Term> rhs_terms;

        bool lhs_is_linear = collect_linear_terms(lhs, lhs_terms, scope);
        bool rhs_is_linear = collect_linear_terms(rhs, rhs_terms, scope);

        if (lhs_is_linear && rhs_is_linear) {
            int lhs_var = find_var(lhs_terms);
            int rhs_var = find_var(rhs_terms);

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
                rhs = simplify(Cast::make(op->a.type(), rhs / var_term.coeff));
                lhs = simplify(Cast::make(op->a.type(), var_term.var));
                solved = true;
            }

            expr = Cmp::make(lhs, rhs);
        } else {
            expr = op;
        }
    }

    template<class Cmp, class Op>
    void visit_asym_cmp(const Cmp *op) {
        Expr lhs = op->a;
        Expr rhs = op->b;

        std::vector<Term> lhs_terms;
        std::vector<Term> rhs_terms;

        bool lhs_is_linear = collect_linear_terms(lhs, lhs_terms, scope);
        bool rhs_is_linear = collect_linear_terms(rhs, rhs_terms, scope);

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
                rhs = simplify(Cast::make(op->a.type(), rhs / var_term.coeff));
                lhs = simplify(Cast::make(op->a.type(), var_term.var));
                solved = true;
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

    void visit(const EQ *op) {visit_sym_cmp<EQ>(op);}
    void visit(const NE *op) {visit_sym_cmp<NE>(op);}
    void visit(const LT *op) {visit_asym_cmp<LT, GT>(op);}
    void visit(const LE *op) {visit_asym_cmp<LE, GE>(op);}
    void visit(const GT *op) {visit_asym_cmp<GT, LT>(op);}
    void visit(const GE *op) {visit_asym_cmp<GE, LE>(op);}

    void visit(const Let *op) {
        scope.push(op->name, op->value);
        mutate(op->body);
        scope.pop(op->name);
    }
};

Expr solve_for_linear_variable(Expr e, Var x) {
    SolveForLinearVariable solver(x.name());
    Expr s = solver.mutate(e);
    if (solver.solved) {
        return s;
    } else {
        return e;
    }
}

Expr solve_for_linear_variable(Expr e, Var x, const Scope<Expr> &scope) {
    SolveForLinearVariable solver(x.name(), &scope);
    Expr s = solver.mutate(e);
    if (solver.solved) {
        return s;
    } else {
        return e;
    }
}

}
}
