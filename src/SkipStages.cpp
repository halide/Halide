#include "SkipStages.h"
#include "IRMutator.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class PredicateFinder : public IRVisitor {
public:
    Expr predicate;
    PredicateFinder(const string &b) : predicate(const_false()), buffer(b), varies(false) {}

private:

    using IRVisitor::visit;
    string buffer;
    bool varies;
    Scope<int> varying;

    void visit(const Variable *op) {
        bool this_varies = varying.contains(op->name);
        varies |= this_varies;
    }

    void visit(const For *op) {
        op->min.accept(this);
        op->extent.accept(this);
        if (!is_one(op->extent)) {
            varying.push(op->name, 0);
        }
        op->body.accept(this);
        if (!is_one(op->extent)) {
            varying.pop(op->name);
        } else {
            predicate = substitute(op->name, op->min, predicate);
        }
    }

    template<typename T>
    void visit_let(const std::string &name, Expr value, T body) {
        bool old_varies = varies;
        varies = false;
        value.accept(this);
        bool value_varies = varies;
        varies |= old_varies;
        if (value_varies) {
            varying.push(name, 0);
        }
        body.accept(this);
        if (value_varies) {
            varying.pop(name);
        }
        predicate = substitute(name, value, predicate);
    }

    void visit(const LetStmt *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const Let *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const Pipeline *op) {
        if (op->name != buffer) {
            op->produce.accept(this);
            if (op->update.defined()) {
                op->update.accept(this);
            }
        }
        op->consume.accept(this);
    }

    template<typename T>
    void visit_conditional(Expr condition, T true_case, T false_case) {
        Expr old_predicate = predicate;

        predicate = const_false();
        true_case.accept(this);
        Expr true_predicate = predicate;

        predicate = const_false();
        if (false_case.defined()) false_case.accept(this);
        Expr false_predicate = predicate;

        bool old_varies = varies;
        predicate = const_false();
        varies = false;
        condition.accept(this);

        if (varies) {
            predicate = (old_predicate || predicate ||
                         true_predicate || false_predicate);
        } else {
            predicate = (old_predicate || predicate ||
                         (condition && true_predicate) ||
                         ((!condition) && false_predicate));
        }

        varies = varies || old_varies;
    }

    void visit(const Select *op) {
        visit_conditional(op->condition, op->true_value, op->false_value);
    }

    void visit(const IfThenElse *op) {
        visit_conditional(op->condition, op->then_case, op->else_case);
    }

    void visit(const Call *op) {
        IRVisitor::visit(op);

        if (op->name == buffer) {
            predicate = const_true();
        }
    }
};

class ProductionGuarder : public IRMutator {
public:
    ProductionGuarder(const string &b, Expr p): buffer(b), predicate(p) {}
private:
    string buffer;
    Expr predicate;

    using IRMutator::visit;

    void visit(const Pipeline *op) {
        if (op->name == buffer) {
            Stmt produce = op->produce, update = op->update;
            if (update.defined()) {
                Expr predicate_var = Variable::make(Bool(), buffer + ".needed");
                Stmt produce = IfThenElse::make(predicate_var, op->produce);
                Stmt update = IfThenElse::make(predicate_var, op->update);
                stmt = Pipeline::make(op->name, produce, update, op->consume);
                stmt = LetStmt::make(buffer + ".needed", predicate, stmt);
            } else {
                Stmt produce = IfThenElse::make(predicate, op->produce);
                stmt = Pipeline::make(op->name, produce, Stmt(), op->consume);
            }

        } else {
            IRMutator::visit(op);
        }
    }
};

class StageSkipper : public IRMutator {
public:
    StageSkipper(const string &b) : buffer(b) {}
private:
    string buffer;
    using IRMutator::visit;

    void visit(const Realize *op) {
        if (op->name == buffer) {
            PredicateFinder f(op->name);
            op->body.accept(&f);
            Expr predicate = simplify(f.predicate);
            debug(3) << "Realization " << op->name << " only used when " << predicate << "\n";
            if (!is_one(predicate)) {
                ProductionGuarder g(op->name, predicate);
                Stmt body = g.mutate(op->body);
                // In the future we may be able to shrink the size
                // opated, but right now those values may be
                // loaded. They can be incorrect, but they must be
                // loadable. Perhaps we can mmap some readable junk memory
                // (e.g. lots of pages of /dev/zero).
                stmt = Realize::make(op->name, op->types, op->bounds, op->lazy, body);
            } else {
                IRMutator::visit(op);
            }
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt skip_stages(Stmt stmt, const vector<string> &order) {
    for (size_t i = order.size()-1; i > 0; i--) {
        StageSkipper skipper(order[i-1]);
        stmt = skipper.mutate(stmt);
    }
    return stmt;
}

}
}
