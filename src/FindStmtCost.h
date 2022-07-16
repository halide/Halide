#ifndef FINDSTMTCOST_H
#define FINDSTMTCOST_H

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRMutator.h"
#include "Module.h"

#include <stdexcept>
#include <unordered_map>

using namespace Halide;
using namespace Internal;
using namespace std;

#define DEPTH_COST 3
#define NUMBER_COST_COLORS 20

#define m_assert(expr, msg) assert((void(msg), (expr)))

struct StmtCost {
    int cost;   // per line
    int depth;  // per nested loop

    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

class FindStmtCost : public IRMutator {

public:
    // constructor
    FindStmtCost() {
    }

    // destructor
    ~FindStmtCost() {
    }

    // starts the traveral based on Module
    void traverse(const Module &m) {
        // recursively traverse all submodules
        for (const auto &s : m.submodules()) {
            traverse(s);
        }

        // traverse all functions
        for (const auto &f : m.functions()) {
            mutate(f.body);
        }

        return;
    }

    Expr mutate(const Expr &expr) override {
        return IRMutator::mutate(expr);
    }

    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

    // returns the range of node based on its
    int get_range(const IRNode *op) const {
        if (op == nullptr) {
            cout << "OH NO ITS NULL!!!!! " << endl;
            return -1;
        }

        // get max value of cost in stmt_cost map
        int max_cost = 0;
        for (auto const &pair : stmt_cost) {
            if (calculate_cost(pair.second) > max_cost) {
                max_cost = calculate_cost(pair.second);
            }
        }

        // divide max cost by 8 and round up to get ranges
        int range_size = (max_cost / NUMBER_COST_COLORS) + 1;
        int cost = get_total_cost(op);
        int range = cost / range_size;
        return range;
    }

    // calculates the total cost of a stmt
    int get_total_cost(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            m_assert(false, "node not found in stmt_cost");
            return 0;
        }

        StmtCost cost_node = it->second;

        return calculate_cost(cost_node);
    }

    int get_depth(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            m_assert(false, "node not found in stmt_cost");
            return 0;
        }

        StmtCost cost_node = it->second;

        return cost_node.depth;
    }

private:
    // create variable that will hold mapping of stmt to cost
    unordered_map<const IRNode *, StmtCost> stmt_cost;

    // stores current loop depth level
    int current_loop_depth = 0;

    // gets cost from `stmt_cost` map
    int get_cost(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            m_assert(false, "node not found in stmt_cost");
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

    // calculates cost of a signle StmtCost object
    int calculate_cost(StmtCost cost_node) const {
        int cost = cost_node.cost;
        int depth = cost_node.depth;

        return cost + DEPTH_COST * depth;
    }

    void print_map(unordered_map<const IRNode *, StmtCost> const &m) {
        for (auto const &pair : m) {
            cout << "{" << pair.first << ": " << pair.second.cost << "}\n";
        }
    }

    Expr visit(const IntImm *op) override {
        set_cost(op, 1);
        return op;
    }

    Expr visit(const UIntImm *op) override {
        set_cost(op, 1);
        return op;
    }

    Expr visit(const FloatImm *op) override {
        set_cost(op, 1);
        return op;
    }

    Expr visit(const StringImm *op) override {
        set_cost(op, 1);
        return op;
    }

    Expr visit(const Cast *op) override {
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Variable *op) override {
        set_cost(op, 1);
        return op;
    }

    Expr visit(const Add *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Sub *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Mul *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Div *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Mod *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Min *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Max *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const EQ *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const NE *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const LT *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const LE *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const GT *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const GE *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const And *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Or *op) override {
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Not *op) override {
        mutate(op->a);
        int tempVal = get_cost(op->a.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    // TODO: do we agree on my counts?
    Expr visit(const Select *op) override {
        mutate(op->condition);
        mutate(op->true_value);
        mutate(op->false_value);

        int tempVal = get_cost(op->condition.get()) + get_cost(op->true_value.get()) + get_cost(op->false_value.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Load *op) override {
        mutate(op->predicate);
        mutate(op->index);
        int tempVal = get_cost(op->predicate.get()) + get_cost(op->index.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Ramp *op) override {
        mutate(op->base);
        mutate(op->stride);
        int tempVal = get_cost(op->base.get()) + get_cost(op->stride.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Broadcast *op) override {
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Call *op) override {
        int tempVal = 0;
        for (const auto &arg : op->args) {
            mutate(arg);
            tempVal += get_cost(arg.get());
        }

        // Consider extern call args
        if (op->func.defined()) {
            Function f(op->func);
            if (op->call_type == Call::Halide && f.has_extern_definition()) {
                for (const auto &arg : f.extern_arguments()) {
                    if (arg.is_expr()) {
                        mutate(arg.expr);
                        tempVal += get_cost(arg.expr.get());
                    }
                }
            }
        }
        set_cost(op, 1 + tempVal);
        return op;
    }

    Expr visit(const Let *op) override {
        mutate(op->value);
        mutate(op->body);
        int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Expr visit(const Shuffle *op) override {
        int tempVal = 0;
        for (const Expr &i : op->vectors) {
            mutate(i);
            tempVal += get_cost(i.get());
        }
        set_cost(op, tempVal);
        return op;
    }

    Expr visit(const VectorReduce *op) override {
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        int countCost = op->value.type().lanes() - 1;

        set_cost(op, tempVal + countCost);
        return op;
    }

    Stmt visit(const LetStmt *op) override {
        mutate(op->value);
        mutate(op->body);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Stmt visit(const AssertStmt *op) override {
        mutate(op->condition);
        mutate(op->message);
        int tempVal = get_cost(op->condition.get()) + get_cost(op->message.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Stmt visit(const ProducerConsumer *op) override {
        mutate(op->body);
        int tempVal = get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const For *op) override {
        current_loop_depth += 1;

        mutate(op->min);
        mutate(op->extent);
        mutate(op->body);

        current_loop_depth -= 1;

        int bodyCost = get_cost(op->body.get());

        // TODO: how to take into account the different types of for loops?
        if (op->for_type == ForType::Parallel) {
            m_assert(false, "Parallel for loops are not supported yet");
        }
        if (op->for_type == ForType::Unrolled) {
            m_assert(false, "Unrolled for loops are not supported yet");
        }
        if (op->for_type == ForType::Vectorized) {
            m_assert(false, "Vectorized for loops are not supported yet");
        }
        set_cost(op, 1 + bodyCost);
        return op;
    }

    Stmt visit(const Acquire *op) override {
        /*
            TODO: change this

                    * depends on contention (how many other accesses are there to this
                      particular semaphore?)
                    * need to have separate visitor that visits everything and accumulates
                      the number of times each lock is accessed, and also keep track of the depth
                      of said lock (the deeper, the more times it will be accessed)
        */
        mutate(op->semaphore);
        mutate(op->count);
        mutate(op->body);
        int tempVal = get_cost(op->semaphore.get()) + get_cost(op->count.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Store *op) override {
        mutate(op->predicate);
        mutate(op->value);
        mutate(op->index);

        int tempVal = get_cost(op->predicate.get()) + get_cost(op->value.get()) + get_cost(op->index.get());
        set_cost(op, 1 + tempVal);
        return op;
    }

    Stmt visit(const Provide *op) override {
        int tempVal = get_cost(op->predicate.get());
        for (const auto &value : op->values) {
            mutate(value);
            tempVal += get_cost(value.get());
        }
        for (const auto &arg : op->args) {
            mutate(arg);
            tempVal += get_cost(arg.get());
        }
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Allocate *op) override {
        /*
            TODO: treat this node differently

                * loop depth is important
                * type of allocation is especially important (heap vs stack)
                      this can be found MemoryType of the Allocate node (might need
                      some nesting to find the node with this type)
                * could visit `extents` for costs, and `condition`
                * (in case of GPUShared type) visualize size of allocation in case
                  the size of shared memory and goes into main memory
        */
        int tempVal = 0;
        for (const auto &extent : op->extents) {
            mutate(extent);
            tempVal += get_cost(extent.get());
        }
        mutate(op->condition);
        tempVal += get_cost(op->condition.get());

        if (op->new_expr.defined()) {
            mutate(op->new_expr);
            tempVal += get_cost(op->new_expr.get());
        }
        mutate(op->body);
        tempVal += get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Free *op) override {
        // TODO: i feel like this should be more than cost 1, but the only
        //       vars it has is the name, which isn't helpful in determining
        //       the cost of the free
        set_cost(op, 1);
        return op;
    }

    Stmt visit(const Realize *op) override {
        // TODO: is this the same logic as For, where I add the depth?
        int tempVal = 0;
        for (const auto &bound : op->bounds) {
            mutate(bound.min);
            mutate(bound.extent);
            tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        }
        mutate(op->condition);
        mutate(op->body);
        tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Prefetch *op) override {
        /*
            TODO: like caching? # of memory stores
        */
        int tempVal = 0;
        for (const auto &bound : op->bounds) {
            mutate(bound.min);
            mutate(bound.extent);

            tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        }
        mutate(op->condition);
        mutate(op->body);
        tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Block *op) override {
        // TODO: making this cost 1 is wrong - need to change this
        int tempVal = 0;
        mutate(op->first);
        tempVal += get_cost(op->first.get());
        if (op->rest.defined()) {
            mutate(op->rest);
            tempVal += get_cost(op->rest.get());
        }
        // set_cost(op, tempVal);
        set_cost(op, 1);
        return op;
    }

    Stmt visit(const Fork *op) override {
        int tempVal = 0;
        mutate(op->first);
        tempVal += get_cost(op->first.get());
        if (op->rest.defined()) {
            mutate(op->rest);
            tempVal += get_cost(op->rest.get());
        }
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const IfThenElse *op) override {
        mutate(op->condition);
        mutate(op->then_case);

        int tempVal = get_cost(op->condition.get()) + get_cost(op->then_case.get());
        if (op->else_case.defined()) {
            mutate(op->else_case);
            tempVal += get_cost(op->else_case.get());
        }
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Evaluate *op) override {
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, tempVal);
        return op;
    }

    Stmt visit(const Atomic *op) override {
        /*
            TODO: change this

                    * make it similar to acquire
                    * parallel vs vector is important
        */
        mutate(op->body);
        int tempVal = get_cost(op->body.get());
        set_cost(op, tempVal);
        return op;
    }

    // Expr visit(const IntImm *op) override;
    // Expr visit(const FloatImm *op) override;
    // Expr visit(const StringImm *op) override;
    // Expr visit(const Cast *op) override;
    // Expr visit(const Variable *op) override;
    // Expr visit(const Add *op) override;
    // Expr visit(const Sub *op) override;
    // Expr visit(const Mul *op) override;
    // Expr visit(const Div *op) override;
    // Expr visit(const Mod *op) override;
    // Expr visit(const Min *op) override;
    // Expr visit(const Max *op) override;
    // Expr visit(const EQ *op) override;
    // Expr visit(const NE *op) override;
    // Expr visit(const LT *op) override;
    // Expr visit(const LE *op) override;
    // Expr visit(const GT *op) override;
    // Expr visit(const GE *op) override;
    // Expr visit(const And *op) override;
    // Expr visit(const Or *op) override;
    // Expr visit(const Not *op) override;
    // Expr visit(const Select *op) override;
    // Expr visit(const Load *op) override;
    // Expr visit(const Ramp *op) override;
    // Expr visit(const Broadcast *op) override;
    // Expr visit(const Call *op) override;
    // Expr visit(const Let *op) override;
    // Expr visit(const Shuffle *op) override;
    // Expr visit(const VectorReduce *op) override;
    // Stmt visit(const LetStmt *op) override;
    // Stmt visit(const AssertStmt *op) override;
    // Stmt visit(const ProducerConsumer *op) override;
    // Stmt visit(const For *op) override;
    // Stmt visit(const Acquire *op) override;
    // Stmt visit(const Store *op) override;
    // Stmt visit(const Provide *op) override;
    // Stmt visit(const Allocate *op) override;
    // Stmt visit(const Free *op) override;
    // Stmt visit(const Realize *op) override;
    // Stmt visit(const Prefetch *op) override;
    // Stmt visit(const Block *op) override;
    // Stmt visit(const Fork *op) override;
    // Stmt visit(const IfThenElse *op) override;
    // Stmt visit(const Evaluate *op) override;
    // Stmt visit(const Atomic *op) override;
};

#endif  // FINDSTMTCOST_H
