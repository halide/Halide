#include "Halide.h"
#include "expr_util.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::ostringstream;
using std::set;
using std::string;

IRNodeType node_ordering[18] = {IRNodeType::Ramp, IRNodeType::Broadcast, IRNodeType::Select, IRNodeType::Div, IRNodeType::Mul, IRNodeType::Mod, IRNodeType::Sub, IRNodeType::Add, IRNodeType::Min, IRNodeType::Not, IRNodeType::Or, IRNodeType::And, IRNodeType::GE, IRNodeType::GT, IRNodeType::LE, IRNodeType::LT, IRNodeType::NE, IRNodeType::EQ};

std::map<IRNodeType, int> nto = {
    {IRNodeType::Ramp, 23},
    {IRNodeType::Broadcast, 22},
    {IRNodeType::Select, 21},
    {IRNodeType::Div, 20},
    {IRNodeType::Mul, 19},
    {IRNodeType::Mod, 18},
    {IRNodeType::Sub, 17},
    {IRNodeType::Add, 16},
    {IRNodeType::Max, 14},  // max and min have same weight
    {IRNodeType::Min, 14},
    {IRNodeType::Not, 13},
    {IRNodeType::Or, 12},
    {IRNodeType::And, 11},
    {IRNodeType::GE, 10},
    {IRNodeType::GT, 9},
    {IRNodeType::LE, 8},
    {IRNodeType::LT, 7},
    {IRNodeType::NE, 6},
    {IRNodeType::EQ, 5},
    {IRNodeType::Cast, 4},
    {IRNodeType::FloatImm, 2},
    {IRNodeType::UIntImm, 1},
    {IRNodeType::IntImm, 0}};

class DivisorSet : public IRVisitor {
    Scope<> lets;

    void visit(const Div *op) override {
        std::ostringstream term;
        term << op->b;
        divisors.insert(term.str());
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Mod *op) override {
        std::ostringstream term;
        term << op->b;
        divisors.insert(term.str());
        op->a.accept(this);
        op->b.accept(this);
    }

public:
    std::set<std::string> divisors;
};

std::set<std::string> find_divisors(const Expr &e) {
    DivisorSet d;
    e.accept(&d);
    return d.divisors;
}

class VectorOpCount : public IRVisitor {
    void visit(const Ramp *op) override {
        counter += 1;
    }
    void visit(const Broadcast *op) override {
        counter += 1;
    }

public:
    int counter = 0;
};

int get_vector_count(const Expr &e) {
    VectorOpCount rcounter;
    e.accept(&rcounter);
    return rcounter.counter;
}

bool check_divisors(const Expr &LHS, const Expr &RHS) {
    // check that all divisors on RHS appear as divisors on LHS
    std::set<std::string> lhs_divisors = find_divisors(LHS);
    std::set<std::string> rhs_divisors = find_divisors(RHS);
    for (auto const &rhs_term : rhs_divisors) {
        if (lhs_divisors.count(rhs_term) == 0) {
            return false;
        }
    }
    return true;
}

class NonlinearOpsCount : public IRVisitor {
    void visit(const Div *op) override {
        counter += 1;
        op->a.accept(this);
        op->b.accept(this);
    }
    void visit(const Mod *op) override {
        counter += 1;
        op->a.accept(this);
        op->b.accept(this);
    }
    void visit(const Mul *op) override {
        counter += 1;
        op->a.accept(this);
        op->b.accept(this);
    }

public:
    int counter = 0;
};

int get_nonlinear_op_count(const Expr &e) {
    NonlinearOpsCount nl;
    e.accept(&nl);
    return nl.counter;
}

bool is_expr_constant(const Expr &e) {
    const Variable *var_a = e.as<Variable>();
    const Call *call_a = e.as<Call>();
    return is_const(e) || (var_a && var_a->name[0] == 'c') || (call_a && call_a->name == "fold");
}

bool is_expr_addsub(const Expr &e) {
    return e.as<Add>() || e.as<Sub>();
}

Expr get_right_child(const Expr &e) {
    if (const Add *op = e.as<Add>()) {
        return op->b;
    } else if (const Sub *op = e.as<Sub>()) {
        return op->b;
    } else if (const Mod *op = e.as<Mod>()) {
        return op->b;
    } else if (const Div *op = e.as<Div>()) {
        return op->b;
    } else if (const Mul *op = e.as<Mul>()) {
        return op->b;
    } else if (const Min *op = e.as<Min>()) {
        return op->b;
    } else if (const Max *op = e.as<Max>()) {
        return op->b;
    } else if (const EQ *op = e.as<EQ>()) {
        return op->b;
    } else if (const NE *op = e.as<NE>()) {
        return op->b;
    } else if (const LT *op = e.as<LT>()) {
        return op->b;
    } else if (const LE *op = e.as<LE>()) {
        return op->b;
    } else if (const And *op = e.as<And>()) {
        return op->b;
    } else if (const Or *op = e.as<Or>()) {
        return op->b;
    } else {
        debug(0) << "Warning: don't know about the right child of: " << e << "\n";
        return Expr();
    }
}

bool is_right_child_constant(const Expr &e) {
    Expr r = get_right_child(e);
    return r.defined() && is_expr_constant(r);
}

class NodeHistogram : public IRVisitor {
    Scope<> lets;

    void visit(const Call *op) override {
        if (op->name == "fold") return;
        IRVisitor::visit(op);
    }

    void visit(const Select *op) override {
        increment_histo(IRNodeType::Select);
        op->condition.accept(this);
        op->true_value.accept(this);
        op->false_value.accept(this);
    }

    void visit(const Ramp *op) override {
        increment_histo(IRNodeType::Ramp);
        op->base.accept(this);
        op->stride.accept(this);
    }

    void visit(const Broadcast *op) override {
        increment_histo(IRNodeType::Broadcast);
        op->value.accept(this);
    }

    void visit(const Add *op) override {
        increment_histo(IRNodeType::Add);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Sub *op) override {
        increment_histo(IRNodeType::Add);  // Put Sub counts in the Add bucket
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Mul *op) override {
        increment_histo(IRNodeType::Mul);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Div *op) override {
        increment_histo(IRNodeType::Div);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Mod *op) override {
        increment_histo(IRNodeType::Mod);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const LT *op) override {
        increment_histo(IRNodeType::LT);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const LE *op) override {
        increment_histo(IRNodeType::LE);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const GT *op) override {
        increment_histo(IRNodeType::GT);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const GE *op) override {
        increment_histo(IRNodeType::GE);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const EQ *op) override {
        increment_histo(IRNodeType::EQ);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Min *op) override {
        increment_histo(IRNodeType::Min);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Max *op) override {
        increment_histo(IRNodeType::Min);  // put max counts into min bucket so we count them the same
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Not *op) override {
        increment_histo(IRNodeType::Not);
        op->a.accept(this);
    }

    void visit(const And *op) override {
        increment_histo(IRNodeType::And);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Or *op) override {
        increment_histo(IRNodeType::Or);
        op->a.accept(this);
        op->b.accept(this);
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        {
            ScopedBinding<> bind(lets, op->name);
            op->body.accept(this);
        }
    }

public:
    std::map<IRNodeType, int> histogram;
    void increment_histo(IRNodeType node_type) {
        if (histogram.count(node_type) == 0) {
            histogram[node_type] = 1;
        } else {
            histogram[node_type] = histogram[node_type] + 1;
        }
    }
};

std::map<IRNodeType, int> build_histogram(const Expr &e) {
    NodeHistogram histo;
    e.accept(&histo);
    return histo.histogram;
}

int get_total_leaf_count(const Expr &e) {
    class CountLeaves : public IRVisitor {
        using IRVisitor::visit;
        void visit(const IntImm *op) override {
            count++;
        }
        void visit(const UIntImm *op) override {
            count++;
        }
        void visit(const FloatImm *op) override {
            count++;
        }
        void visit(const Variable *op) override {
            count++;
        }
        void visit(const Call *op) override {
            if (op->name == "fold") {
                count++;
            } else {
                IRVisitor::visit(op);
            }
        }

    public:
        int count = 0;
    } counter;
    e.accept(&counter);
    return counter.count;
}

int get_total_op_count(const Expr &e) {
    std::map<IRNodeType, int> histo = build_histogram(e);
    int counter = 0;
    for (auto const &node : histo) {
        counter += node.second;
    }
    return counter;
}

// return 1 if correctly ordered, -1 if incorrectly ordered, 0 if tied
int compare_histograms(const Expr &LHS, const Expr &RHS) {
    std::map<IRNodeType, int> lhs_histo = build_histogram(LHS);
    std::map<IRNodeType, int> rhs_histo = build_histogram(RHS);
    int lhs_node_count, rhs_node_count;
    for (auto const &node : node_ordering) {
        lhs_node_count = 0;
        rhs_node_count = 0;
        if (lhs_histo.count(node) == 1) {
            lhs_node_count = lhs_histo[node];
        }
        if (rhs_histo.count(node) == 1) {
            rhs_node_count = rhs_histo[node];
        }

        // std::cout << node << " LHS count " << lhs_node_count << " RHS count " << rhs_node_count << "\n";
        // RHS side has more of some op than LHS
        if (lhs_node_count < rhs_node_count) {
            return -1;
            // LHS side has strictly more of some op than RHS
        } else if (lhs_node_count > rhs_node_count) {
            return 1;
        }
    }
    return 0;
}

bool valid_reduction_order(const Expr &LHS, const Expr &RHS) {

    // first, check that RHS has fewer ramp ops
    // wildcard variables can only match scalars, so we don't need to check variable occurence counts
    if (get_vector_count(LHS) > get_vector_count(RHS)) {
        return true;
    } else if (get_vector_count(LHS) < get_vector_count(RHS)) {
        return false;
    }

    // check that occurrences of variables on RHS is equal or lesser to those in LHS
    // if any variable has more occurrences in RHS than it does on LHS, then the next several orders are invalid
    auto lhs_vars = find_vars(LHS);
    auto rhs_vars = find_vars(RHS);
    for (auto const &varcount : rhs_vars) {
        // constant wildcards don't count bc they can't match terms so can't cause reduction order failures
        if (varcount.first.front() != 'c' &&
            (lhs_vars.count(varcount.first) == 0 ||
             varcount.second.second > lhs_vars[varcount.first].second)) {
            return false;
        }
    }

    // accept rule if LHS has strictly more occurrences of at least 1 variable
    for (auto const &lhsv : lhs_vars) {
        if ((lhsv.first.front() != 'c') &&
            ((rhs_vars.count(lhsv.first) == 0) || (lhsv.second.second > rhs_vars[lhsv.first].second))) {
            return true;
        }
    }

    // LHS should have more div, mod, mul operations than RHS (if var occurrences are >=)
    if (get_nonlinear_op_count(LHS) > get_nonlinear_op_count(RHS)) {
        return true;
    } else if (get_nonlinear_op_count(LHS) < get_nonlinear_op_count(RHS)) {
        return false;
    }

    // LHS should have more total ops than RHS (if var occurrences are >=)
    if (get_total_leaf_count(LHS) > get_total_leaf_count(RHS)) {
        return true;
    } else if (get_total_leaf_count(LHS) < get_total_leaf_count(RHS)) {
        return false;
    }

    // LHS should have more total ops than RHS (if var occurrences are >=)
    if (get_total_op_count(LHS) > get_total_op_count(RHS)) {
        return true;
    } else if (get_total_op_count(LHS) < get_total_op_count(RHS)) {
        return false;
    }

    // check that histogram of operations obeys ordering (if var occurrences are >=)
    int rule_histogram_ordering = compare_histograms(LHS, RHS);
    if (rule_histogram_ordering == 1) {
        return true;
    } else if (rule_histogram_ordering == -1) {
        return false;
    }

    // ordered if LHS is not add or sub and RHS is add or sub
    // invalid order if LHS is add or sub and RSH is NOT add or sub
    bool is_LHS_add_sub = is_expr_addsub(LHS);
    bool is_RHS_add_sub = is_expr_addsub(RHS);

    if (!(is_LHS_add_sub) && is_RHS_add_sub) {
        return true;
    }
    if (is_LHS_add_sub && !(is_RHS_add_sub)) {
        return false;
    }

    // ordered if the right child of the LHS is not a constant and the right child of the RHS is a constant
    // invalid order if the right child of the LHS is a constant and the right child of the RHS is not a constant
    // this checks if right child is IntImm, UIntImm, or Variable whose first char is c
    if (!(is_right_child_constant(LHS)) && is_right_child_constant(RHS)) {
        return true;
    }
    if (is_right_child_constant(LHS) && !(is_right_child_constant(RHS))) {
        return false;
    }

    // check that root symbol obeys ordering
    IRNodeType lhs_root_type = LHS.node_type();
    IRNodeType rhs_root_type = RHS.node_type();

    if (nto[lhs_root_type] < nto[rhs_root_type]) {
        return true;
    }
    if (nto[lhs_root_type] > nto[rhs_root_type]) {
        return false;
    }

    // It's a tie. No good.
    return false;
}
