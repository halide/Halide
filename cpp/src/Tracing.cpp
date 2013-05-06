#include "Tracing.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::vector;

class InjectTracing : public IRMutator {
public:
    int level;
    InjectTracing() {
        char *trace = getenv("HL_TRACE");
        level = trace ? atoi(trace) : 0;
    }


private:
    using IRMutator::visit;

    void visit(const Call *op) {
        expr = op;
    }

    void visit(const Provide *op) {       
        IRMutator::visit(op);
        // We print every provide at tracing level 3 or higher
        if (level >= 3) {
            const Provide *op = stmt.as<Provide>();
            vector<Expr> args = op->args;
            args.push_back(op->value);
            Stmt print = new PrintStmt("Provide " + op->name, args);
            stmt = new Block(print, op);
        }
    }

    void visit(const Realize *op) {
        IRMutator::visit(op);
        if (level >= 1) {
            const Realize *op = stmt.as<Realize>();
            vector<Expr> args;
            for (size_t i = 0; i < op->bounds.size(); i++) {
                args.push_back(op->bounds[i].min);
                args.push_back(op->bounds[i].extent);
            }
            Expr time = new Call(Int(32), "halide_current_time", std::vector<Expr>());
            Stmt print = new PrintStmt("Realizing " + op->name + " over ", args);
            Stmt start_time = new PrintStmt("Starting realization of " + op->name + " at time ", vec(time));
            Stmt body = new Block(new Block(start_time, print), op->body);
            stmt = new Realize(op->name, op->type, op->bounds, body);
        }        
    }

    void visit(const Pipeline *op) {
        if (level >= 1) {
            Expr time = new Call(Int(32), "halide_current_time", std::vector<Expr>());
            Stmt print_produce = new PrintStmt("Producing " + op->name + " at time ", vec(time));
            Stmt print_update = new PrintStmt("Updating " + op->name + " at time ", vec(time));
            Stmt print_consume = new PrintStmt("Consuming " + op->name + " at time ", vec(time));
            Stmt produce = mutate(op->produce);
            Stmt update = op->update.defined() ? mutate(op->update) : Stmt();
            Stmt consume = mutate(op->consume);
            produce = new Block(print_produce, produce);
            update = update.defined() ? new Block(print_update, update) : Stmt();
            consume = new Block(print_consume, consume);
            stmt = new Pipeline(op->name, produce, update, consume);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const For *op) {
        // We only enter for loops at tracing level 2 or higher
        if (level >= 2) {
            IRMutator::visit(op);
        } else {
            stmt = op;
        }
    }
};

Stmt inject_tracing(Stmt s) {
    InjectTracing tracing;
    s = tracing.mutate(s);
    if (tracing.level >= 1) {
        Expr time = new Call(Int(32), "halide_current_time", std::vector<Expr>());
        Expr start_clock_call = new Call(Int(32), "halide_start_clock", std::vector<Expr>());
        Stmt start_clock = new AssertStmt(start_clock_call == 0, "Failed to start clock");
        Stmt print_final_time = new PrintStmt("Total time: ", vec(time));
        s = new Block(new Block(start_clock, s), print_final_time);
    }
    return s;
}

}
}
