#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"

// This file is largely a port of src/bounds.ml

namespace Halide { namespace Internal {
        class Bounds : public IRVisitor {
            Expr min, max;
            Scope<pair<Expr, Expr> > scope;
            void visit(const IntImm *op) {
                min = op;
                max = op;
            }

            void visit(const FloatImm *op) {
                min = op;
                max = op;
            }

            void visit(const Cast *op) {
                op->accept(this);
                min = new Cast(op->type, min);
                max = new Cast(op->type, max);
            }

            void visit(const Variable *op) {
                if (scope.contains(op->name)) {
                    pair<Expr, Expr> bounds = scope.get(op->name);
                    min = bounds.first;
                    max = bounds.second;
                } else {
                    min = op;
                    max = op;
                }
            }

            void visit(const Add *op) {
                op->a.accept(this);
                Expr min_a = min, max_a = max;
                op->b.accept(this);
                min = new Add(min_a, min);
                max = new Add(max_a, max);
            }

            void visit(const Sub *op) {
                op->a.accept(this);
                Expr min_a = min, max_a = max;
                op->b.accept(this);
                min = new Sub(min_a, max);
                max = new Sub(max_a, min);
            }

            void visit(const Mul *op) {
                // yuck..
            }

            void visit(const Div *);
            void visit(const Mod *);
            void visit(const Min *);
            void visit(const Max *);
            void visit(const EQ *);
            void visit(const NE *);
            void visit(const LT *);
            void visit(const LE *);
            void visit(const GT *);
            void visit(const GE *);
            void visit(const And *);
            void visit(const Or *);
            void visit(const Not *);
            void visit(const Select *);
            void visit(const Load *);
            void visit(const Ramp *);
            void visit(const Broadcast *);
            void visit(const Call *);
            void visit(const Let *);
            void visit(const LetStmt *);
            void visit(const PrintStmt *);
            void visit(const AssertStmt *);
            void visit(const Pipeline *);
            void visit(const For *);
            void visit(const Store *);
            void visit(const Provide *);
            void visit(const Allocate *);
            void visit(const Realize *);
            void visit(const Block *);
        };

    bool bounds_of_expr_in_scope(Expr expr, const Scope<pair<Expr, Expr> > &scope, Expr *min, Expr *max) {
        return false;
    }
}}
