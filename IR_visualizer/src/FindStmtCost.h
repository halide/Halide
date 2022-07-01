#ifndef STMTCOST_H
#define STMTCOST_H

// #include "IRVisitor.h"
// #include <Halide.h>

// #include "ExternFuncArgument.h"
// #include "Function.h"

#include "../../src/IRVisitor.h"
#include <Halide.h>

#include "../../src/ExternFuncArgument.h"
#include "../../src/Function.h"

#include <stdexcept>
#include <unordered_map>

using namespace Halide;
using namespace Internal;

#define DEPTH_COST 3
struct StmtCost {
    int cost;   // per line
    int depth;  // per nested loop

    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

// TODO: change to IRMutator instead of IRVisitor and then
//       call add_custom_lowering_pass()
class FindStmtCost : public IRMutator {

private:
    // create variable that will hold mapping of stmt to cost
    std::unordered_map<const IRNode *, StmtCost> stmt_cost;

    // stores current loop depth level
    int current_loop_depth = 0;

    // gets cost from `stmt_cost` map
    int get_cost(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            assert(false);
            return 0;
        }
        return it->second.cost;
    }

    // sets cost & depth in `stmt_cost` map
    void set_cost(const IRNode *node, int cost) {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            stmt_cost.emplace(node, StmtCost{cost, current_loop_depth});
        } else {
            it->second.cost = cost;
            it->second.depth = current_loop_depth;
        }
    }

    // gets depth from `stmt_cost` map
    int get_depth(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            assert(false);
            return 0;
        }
        return it->second.depth;
    }

    // Expr eval(Func f, int tuple_idx = 0, int updef_idx = -1) {
    //     if (!f.defined()) {
    //         return Expr();
    //     }

    //     if (updef_idx == -1) {
    //         // by default, choose the very last update definition (if any)
    //         updef_idx = f.num_update_definitions();
    //     }

    //     Tuple values{Expr()};
    //     if (updef_idx == 0) {
    //         // pure definition
    //         values = f.values();
    //     } else {
    //         --updef_idx;
    //         assert(updef_idx >= 0);
    //         assert(updef_idx < f.num_update_definitions());
    //         values = f.update_values(updef_idx);
    //     }

    //     assert(tuple_idx < values.size());
    //     Expr value = values[tuple_idx];
    //     return value;
    // }

public:
    // constructor
    FindStmtCost() = default;

    // destructor
    ~FindStmtCost() = default;

    // calculates the total cost of a stmt
    int get_total_cost(const IRNode *node) const;

    // void visit(Expr expr) {
    //     expr.accept(this);
    // }

    // void visit(Func f) {
    //     visit(eval(f));
    // }

    Expr visit(const IntImm *op) override;
    Expr visit(const UIntImm *op) override;
    Expr visit(const FloatImm *op) override;
    Expr visit(const StringImm *op) override;
    Expr visit(const Cast *op) override;
    Expr visit(const Variable *op) override;
    Expr visit(const Add *op) override;
    Expr visit(const Sub *op) override;
    Expr visit(const Mul *op) override;
    Expr visit(const Div *op) override;
    Expr visit(const Mod *op) override;
    Expr visit(const Min *op) override;
    Expr visit(const Max *op) override;
    Expr visit(const EQ *op) override;
    Expr visit(const NE *op) override;
    Expr visit(const LT *op) override;
    Expr visit(const LE *op) override;
    Expr visit(const GT *op) override;
    Expr visit(const GE *op) override;
    Expr visit(const And *op) override;
    Expr visit(const Or *op) override;
    Expr visit(const Not *op) override;
    Expr visit(const Select *op) override;
    Expr visit(const Load *op) override;
    Expr visit(const Ramp *op) override;
    Expr visit(const Broadcast *op) override;
    Expr visit(const Call *op) override;
    Expr visit(const Let *op) override;
    Expr visit(const Shuffle *op) override;
    Expr visit(const VectorReduce *op) override;

    Stmt visit(const LetStmt *op) override;
    Stmt visit(const AssertStmt *op) override;
    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const Acquire *op) override;
    Stmt visit(const Store *op) override;
    Stmt visit(const Provide *op) override;
    Stmt visit(const Allocate *op) override;
    Stmt visit(const Free *op) override;
    Stmt visit(const Realize *op) override;
    Stmt visit(const Prefetch *op) override;
    Stmt visit(const Block *op) override;
    Stmt visit(const Fork *op) override;
    Stmt visit(const IfThenElse *op) override;
    Stmt visit(const Evaluate *op) override;
    Stmt visit(const Atomic *op) override;
};

#endif  // STMTCOST_H
