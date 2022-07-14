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
#define LOW_RANGE 0
#define MEDIUM_RANGE 1
#define HIGH_RANGE 2

#define m_assert(expr, msg) assert((void(msg), (expr)))

struct StmtCost {
    int cost;   // per line
    int depth;  // per nested loop

    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

// TODO: change to IRMutator instead of IRVisitor and then
//       call add_custom_lowering_pass()
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

        cout << "traversing module " << m.name() << endl;

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

    // returns the ranges for low, medium, and high cost
    int get_range(const IRNode *op) const {

        // get max value of cost in stmt_cost map
        int max_cost = 0;
        for (auto const &pair : stmt_cost) {
            if (calculate_cost(pair.second) > max_cost) {
                max_cost = calculate_cost(pair.second);
            }
        }
        // divide max cost by 3 and round up to get ranges
        int range_size = (max_cost / 3) + 1;
        int cost = get_total_cost(op);

        if (0 <= cost && cost < range_size) {
            return LOW_RANGE;
        } else if (range_size <= cost && cost < 2 * range_size) {
            return MEDIUM_RANGE;
        } else if (2 * range_size <= cost && cost < 3 * range_size) {
            return HIGH_RANGE;
        } else {
            throw runtime_error("cost out of range");
        }
    }

    // calculates the total cost of a stmt
    // int get_total_cost(const IRNode *node) const;
    int get_total_cost(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            m_assert(false, "node not found in stmt_cost");
            return 0;
        }
        // int cost = it->second.cost;
        // int depth = it->second.depth;

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

    Expr mutate(const Expr &expr) override {
        return IRMutator::mutate(expr);
    }

    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

private:
    // create variable that will hold mapping of stmt to cost
    unordered_map<const IRNode *, StmtCost> stmt_cost;

    // ranges for low, medium, high cost
    // vector<vector<int>> ranges;

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
        // cout << "In Int" << endl;
        // print_map(stmt_cost);
        set_cost(op, 1);
        // cout << "In IntImm, with value: " << 1 << endl;
        return op;
    }

    Expr visit(const UIntImm *op) override {
        // cout << "In UInt" << endl;
        // print_map(stmt_cost);
        set_cost(op, 1);
        // cout << "In UIntImm, with value: " << 1 << endl;
        return op;
    }

    Expr visit(const FloatImm *op) override {
        // cout << "In Float" << endl;
        // print_map(stmt_cost);
        set_cost(op, 1);
        // cout << "In FloatImm, with value: " << 1 << endl;
        return op;
    }

    Expr visit(const StringImm *op) override {
        // cout << "In String" << endl;
        // print_map(stmt_cost);
        set_cost(op, 1);
        // cout << "In StringImm, with value: " << 1 << endl;
        return op;
    }

    Expr visit(const Cast *op) override {
        // cout << "In Cast" << endl;
        // print_map(stmt_cost);
        // op->value.accept(this);
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Cast, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Variable *op) override {
        // cout << "In Variable" << endl;
        // print_map(stmt_cost);
        set_cost(op, 1);
        // cout << "In Variable, with value: " << 1 << endl;
        return op;
    }

    Expr visit(const Add *op) override {
        // cout << "In Add" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Add, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Sub *op) override {
        // cout << "In Sub" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Sub, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Mul *op) override {
        // cout << "In Mul" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Mul, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Div *op) override {
        // cout << "In Div" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Div, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Mod *op) override {
        // cout << "In Mod" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Mod, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Min *op) override {
        // cout << "In Min" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Min, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Max *op) override {
        // cout << "In Max" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Max, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const EQ *op) override {
        // cout << "In EQ" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In EQ, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const NE *op) override {
        // cout << "In NE" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In NE, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const LT *op) override {
        // cout << "In LT" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In LT, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const LE *op) override {
        // cout << "In LE" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In LE, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const GT *op) override {
        // cout << "In GT" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In GT, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const GE *op) override {
        // cout << "In GE" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In GE, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const And *op) override {
        // cout << "In And" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In And, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Or *op) override {
        // cout << "In Or" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        // op->b.accept(this);
        mutate(op->a);
        mutate(op->b);
        int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Or, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Not *op) override {
        // cout << "In Not" << endl;
        // print_map(stmt_cost);
        // op->a.accept(this);
        mutate(op->a);
        int tempVal = get_cost(op->a.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Not, with value: " << 1 + tempVal << endl;
        return op;
    }

    // TODO: do we agree on my counts?
    Expr visit(const Select *op) override {
        // cout << "In Select" << endl;
        // print_map(stmt_cost);
        // op->condition.accept(this);
        // op->true_value.accept(this);
        // op->false_value.accept(this);
        mutate(op->condition);
        mutate(op->true_value);
        mutate(op->false_value);

        int tempVal = get_cost(op->condition.get()) + get_cost(op->true_value.get()) + get_cost(op->false_value.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Select, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Load *op) override {
        // cout << "In Load" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Load not implemented");
        // op->predicate.accept(this);
        // op->index.accept(this);
        mutate(op->predicate);
        mutate(op->index);
        int tempVal = get_cost(op->predicate.get()) + get_cost(op->index.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Load, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Ramp *op) override {
        // cout << "In Ramp" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Ramp not implemented");
        // op->base.accept(this);
        // op->stride.accept(this);
        mutate(op->base);
        mutate(op->stride);
        int tempVal = get_cost(op->base.get()) + get_cost(op->stride.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Ramp, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Broadcast *op) override {
        // cout << "In Broadcast" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Broadcast not implemented");
        // op->value.accept(this);
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Broadcast, with value: " << 1 + tempVal << endl;
        return op;
    }

    Expr visit(const Call *op) override {
        // cout << "In Call" << endl;
        // print_map(stmt_cost);
        int tempVal = 0;
        for (const auto &arg : op->args) {
            // arg.accept(this);
            mutate(arg);
            tempVal += get_cost(arg.get());
        }

        // Consider extern call args
        if (op->func.defined()) {
            Function f(op->func);
            if (op->call_type == Call::Halide && f.has_extern_definition()) {
                for (const auto &arg : f.extern_arguments()) {
                    if (arg.is_expr()) {
                        // arg.expr.accept(this);
                        mutate(arg.expr);
                        tempVal += get_cost(arg.expr.get());
                    }
                }
            }
        }
        set_cost(op, tempVal);
        // cout << "In Call, with value: " << tempVal << endl;
        return op;
    }

    Expr visit(const Let *op) override {
        // cout << "In Let" << endl;
        // print_map(stmt_cost);
        // op->value.accept(this);
        // op->body.accept(this);
        mutate(op->value);
        mutate(op->body);
        int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Let, with value: " << tempVal << endl;
        return op;
    }

    Expr visit(const Shuffle *op) override {
        // cout << "In Shuffle" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Shuffle not implemented");
        int tempVal = 0;
        for (const Expr &i : op->vectors) {
            // i.accept(this);
            mutate(i);
            tempVal += get_cost(i.get());
        }
        set_cost(op, tempVal);
        // cout << "In Shuffle, with value: " << tempVal << endl;
        return op;
    }

    Expr visit(const VectorReduce *op) override {
        // cout << "In Vector" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "VectorReduce not implemented");
        // op->value.accept(this);
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, tempVal);
        // cout << "In Vector, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const LetStmt *op) override {
        // cout << "In Let" << endl;
        // print_map(stmt_cost);
        // op->value.accept(this);
        // op->body.accept(this);
        mutate(op->value);
        mutate(op->body);
        int tempVal = get_cost(op->value.get());
        set_cost(op, 1 + tempVal);
        // cout << "In LetStmt, with value: " << 1 + tempVal << endl;
        return op;
    }

    Stmt visit(const AssertStmt *op) override {
        // cout << "In Assert" << endl;
        // print_map(stmt_cost);
        // op->condition.accept(this);
        // op->message.accept(this);
        mutate(op->condition);
        mutate(op->message);
        int tempVal = get_cost(op->condition.get()) + get_cost(op->message.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Assert, with value: " << 1 + tempVal << endl;
        return op;
    }

    Stmt visit(const ProducerConsumer *op) override {
        // cout << "In Producer" << endl;
        // print_map(stmt_cost);
        // op->body.accept(this);
        mutate(op->body);
        int tempVal = get_cost(op->body.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Producer, with value: " << 1 + tempVal << endl;
        return op;
    }

    Stmt visit(const For *op) override {
        // cout << "In For" << endl;
        // print_map(stmt_cost);
        current_loop_depth += 1;

        // op->min.accept(this);
        // op->extent.accept(this);
        // op->body.accept(this);
        mutate(op->min);
        mutate(op->extent);
        mutate(op->body);

        current_loop_depth -= 1;

        int bodyCost = get_cost(op->body.get());

        // TODO: how to take into account the different types of for loops?
        // if (op->for_type == ForType::Serial) {

        // }
        if (op->for_type == ForType::Parallel) {
            m_assert(false, "For not implemented");
        }
        if (op->for_type == ForType::Unrolled) {
            m_assert(false, "For not implemented");
        }
        if (op->for_type == ForType::Vectorized) {
            m_assert(false, "For not implemented");
        }
        set_cost(op, 1 + bodyCost);
        // cout << "In For, with value: " << 1 + bodyCost << endl;
        return op;
    }

    Stmt visit(const Acquire *op) override {
        // cout << "In Acquire" << endl;
        // print_map(stmt_cost);
        // op->semaphore.accept(this);
        // op->count.accept(this);
        // op->body.accept(this);
        mutate(op->semaphore);
        mutate(op->count);
        mutate(op->body);
        int tempVal = get_cost(op->semaphore.get()) + get_cost(op->count.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Acquire, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Store *op) override {
        // cout << "In Store" << endl;
        // print_map(stmt_cost);
        // op->predicate.accept(this);
        // op->value.accept(this);
        // op->index.accept(this);
        mutate(op->predicate);
        mutate(op->value);
        mutate(op->index);

        int tempVal = get_cost(op->predicate.get()) + get_cost(op->value.get()) + get_cost(op->index.get());
        set_cost(op, 1 + tempVal);
        // cout << "In Store, with value: " << 1 + tempVal << endl;
        return op;
    }

    Stmt visit(const Provide *op) override {
        // cout << "In Provide" << endl;
        // print_map(stmt_cost);
        m_assert(false, "Provide not implemented");
        // op->predicate.accept(this);
        // int tempVal = get_cost(op->predicate.get());
        // for (const auto &value : op->values) {
        //     value.accept(this);
        // mutate(value);
        //     tempVal += get_cost(value.get());
        // }
        // for (const auto &arg : op->args) {
        //     arg.accept(this);
        // mutate(arg);
        //     tempVal += get_cost(arg.get());
        // }
        // set_cost(op, 1 + tempVal);
        // return op;
    }

    Stmt visit(const Allocate *op) override {
        // cout << "In Allocate" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Allocate not implemented");
        int tempVal = 0;
        for (const auto &extent : op->extents) {
            // extent.accept(this);
            mutate(extent);
            tempVal += get_cost(extent.get());
        }
        // op->condition.accept(this);
        mutate(op->condition);
        tempVal += get_cost(op->condition.get());

        if (op->new_expr.defined()) {
            // op->new_expr.accept(this);
            mutate(op->new_expr);
            tempVal += get_cost(op->new_expr.get());
        }
        // op->body.accept(this);
        mutate(op->body);
        tempVal += get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Allocate, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Free *op) override {
        // cout << "In Free" << endl;
        // print_map(stmt_cost);
        // TODO: i feel like this should be more than cost 1, but the only
        //       vars it has is the name, which isn't helpful in determining
        //       the cost of the free
        set_cost(op, 1);
        // cout << "In Free, with value: " << 1 << endl;
        return op;
    }

    Stmt visit(const Realize *op) override {
        // cout << "In Realize" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Realize not implemented");
        // TODO: is this the same logic as For, where I add the depth?
        int tempVal = 0;
        for (const auto &bound : op->bounds) {
            // bound.min.accept(this);
            // bound.extent.accept(this);
            mutate(bound.min);
            mutate(bound.extent);
            tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        }
        // op->condition.accept(this);
        // op->body.accept(this);
        mutate(op->condition);
        mutate(op->body);
        tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Realize, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Prefetch *op) override {
        // cout << "In Prefetch" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Prefetch not implemented");
        // TODO: similar question as one above
        int tempVal = 0;
        for (const auto &bound : op->bounds) {
            // bound.min.accept(this);
            // bound.extent.accept(this);
            mutate(bound.min);
            mutate(bound.extent);

            tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        }
        // op->condition.accept(this);
        // op->body.accept(this);
        mutate(op->condition);
        mutate(op->body);
        tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Prefetch, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Block *op) override {
        // cout << "In Block" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Block not implemented");
        int tempVal = 0;
        // op->first.accept(this);
        mutate(op->first);
        tempVal += get_cost(op->first.get());
        if (op->rest.defined()) {
            // op->rest.accept(this);
            mutate(op->rest);
            tempVal += get_cost(op->rest.get());
        }
        // TODO: edit this later
        // set_cost(op, tempVal);
        set_cost(op, 1);
        // cout << "In Block, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Fork *op) override {
        // cout << "In Fork" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Fork not implemented");
        int tempVal = 0;
        // op->first.accept(this);
        mutate(op->first);
        tempVal += get_cost(op->first.get());
        if (op->rest.defined()) {
            // op->rest.accept(this);
            mutate(op->rest);
            tempVal += get_cost(op->rest.get());
        }
        set_cost(op, tempVal);
        // cout << "In Fork, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const IfThenElse *op) override {
        // cout << "In If" << endl;
        // print_map(stmt_cost);
        // TODO: is this correct, based on discussion about if-then-else, as
        //       compared to Select?
        // op->condition.accept(this);
        // op->then_case.accept(this);
        mutate(op->condition);
        mutate(op->then_case);

        int tempVal = get_cost(op->condition.get()) + get_cost(op->then_case.get());
        if (op->else_case.defined()) {
            // op->else_case.accept(this);
            mutate(op->else_case);
            tempVal += get_cost(op->else_case.get());
        }
        set_cost(op, tempVal);
        // cout << "In If, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Evaluate *op) override {
        // cout << "In Evaluate" << endl;
        // print_map(stmt_cost);
        // op->value.accept(this);
        mutate(op->value);
        int tempVal = get_cost(op->value.get());
        set_cost(op, tempVal);
        // cout << "In Evaluate, with value: " << tempVal << endl;
        return op;
    }

    Stmt visit(const Atomic *op) override {
        // cout << "In Atomic" << endl;
        // print_map(stmt_cost);
        // m_assert(false, "Atomic not implemented");
        // op->body.accept(this);
        mutate(op->body);
        int tempVal = get_cost(op->body.get());
        set_cost(op, tempVal);
        // cout << "In Atomic, with value: " << tempVal << endl;
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
