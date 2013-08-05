#include "Tracing.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

int tracing_level() {
    char *trace = getenv("HL_TRACE");
    return trace ? atoi(trace) : 0;
}

using std::vector;

class InjectTracing : public IRMutator {
public:
    int level;
    InjectTracing() {
        level = tracing_level();
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
            args.insert(args.end(), op->values.begin(), op->values.end());
            Stmt print = PrintStmt::make("Provide " + op->name, args);
            stmt = Block::make(print, op);
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
            Expr time = Call::make(Int(32), "halide_current_time", std::vector<Expr>(), Call::Extern);
            Stmt print = PrintStmt::make("Realizing " + op->name + " over ", args);
            Stmt start_time = PrintStmt::make("Starting realization of " + op->name + " at time ", vec(time));
            Stmt body = Block::make(Block::make(start_time, print), op->body);
            stmt = Realize::make(op->name, op->types, op->bounds, body);
        }
    }

    void visit(const Pipeline *op) {
        if (level >= 1) {
            Expr time = Call::make(Int(32), "halide_current_time", std::vector<Expr>(), Call::Extern);
            Stmt print_produce = PrintStmt::make("Producing " + op->name + " at time ", vec(time));
            Stmt print_update = PrintStmt::make("Updating " + op->name + " at time ", vec(time));
            Stmt print_consume = PrintStmt::make("Consuming " + op->name + " at time ", vec(time));
            Stmt produce = mutate(op->produce);
            Stmt update = op->update.defined() ? mutate(op->update) : Stmt();
            Stmt consume = mutate(op->consume);
            produce = Block::make(print_produce, produce);
            update = update.defined() ? Block::make(print_update, update) : Stmt();
            consume = Block::make(print_consume, consume);
            stmt = Pipeline::make(op->name, produce, update, consume);
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
        Expr time = Call::make(Int(32), "halide_current_time", std::vector<Expr>(), Call::Extern);
        Expr start_clock_call = Call::make(Int(32), "halide_start_clock", std::vector<Expr>(), Call::Extern);
        Stmt start_clock = AssertStmt::make(start_clock_call == 0, "Failed to start clock");
        Stmt print_final_time = PrintStmt::make("Total time: ", vec(time));
        s = Block::make(Block::make(start_clock, s), print_final_time);
    }
    return s;
}

}
}
