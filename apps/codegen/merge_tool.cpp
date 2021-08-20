#include <iostream>
#include <sstream>
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

#include <memory>
#include <string>
#include <vector>
#include <map>

using std::shared_ptr;
using std::vector;
using std::make_shared;

struct VarInfo {
    Halide::Type type;
    std::string name;
    VarInfo(Halide::Type _type, const std::string &_name) : type(_type), name(_name) {}
    VarInfo() {
        assert(false); // This should never happen.
        // TODO: how to properly do this.
    }
};

typedef std::map<std::string, VarInfo> VarScope;

struct RewriteRule {
    const Expr before;
    const Expr after;
    const Expr pred;
};

namespace Printer {

// For code generation

std::string make_type_checker_condition(const std::string &var_name, const std::string &type_name, const std::string &output_name) {
    return "const " + type_name + " *" + output_name + " = " + var_name + ".as<" + type_name + ">()";
}

std::string make_new_unique_name() {
    static int counter = 0;
    return "a" + std::to_string(counter++);
}

class IRPrinterNoType : public IRPrinter {
public:
    IRPrinterNoType(std::ostream &stream) : IRPrinter(stream) {}

protected:
    using IRPrinter::visit;

    void visit(const Variable *op) override {
        stream << op->name;
    }

    // TODO: print in proper C++/Halide (probably)
    // void visit(const Cast *op) override {
    // }
};

std::string pretty_print(const Expr &expr) {
    std::stringstream s;
    IRPrinterNoType printer = s;
    expr.accept(&printer);
    return s.str();
}


std::string build_expr(const Expr &expr, const VarScope &scope) {
    std::map<std::string, Expr> replacements;
    for (const auto &p : scope) {
      replacements[p.first] = Variable::make(p.second.type, p.second.name);
    }
    // TODO: the old method asserted that all variables that exist in `expr` have a match in `scope`,
    //       we should do that here too.
    Expr new_expr = Halide::Internal::substitute(replacements, expr);
    return pretty_print(new_expr);
}

} // namespace Printer


namespace Language {

enum class IRType {
    // Type checks
    Add, Sub, Select, IntImm,
    // TODO: add the rest
    
    // Stmt
    Equality, Return, Condition, Sequence,
};

struct Node {
    virtual void print(std::ostream &stream, std::string indent) const = 0; // This makes the struct abstract.
    virtual bool equal(const shared_ptr<Node> &other) const = 0;
    virtual ~Node() = default; // Otherwise C++ breaks for some reason.

    vector<shared_ptr<Node>> children;
    const IRType type;

    Node(IRType _type) : type(_type) {}

    template<typename T>
    const T *as(IRType _type) const {
        if (type != _type) {
            return nullptr;
        } else {
            return dynamic_cast<const T*>(this);
        }
    }

    // Might want to make this virtual so Return nodes can override it and fail early.
    template<typename T> // T must inherit from Node.
    shared_ptr<T> get_child(shared_ptr<T> _child) {
        auto is_node = [&_child](const shared_ptr<Node> &child) { return _child->equal(child); };
        auto result = std::find_if(children.begin(), children.end(), is_node);
        if (result != children.end()) {
            // This is safe, I think?
            return std::dynamic_pointer_cast<T>(*result);
        } else {
            // Need to insert the child
            children.push_back(_child);
            return _child;
        }
    }
};

template<typename T>
struct TypeCheck : public Node {
    std::string current_name;
    std::string output_name;

    bool equal(const shared_ptr<Node>& other) const override {
        if (const T *other_tc = other->as<T>(type)) {
            // We only care about the object's name (and type of course)
            return (current_name == other_tc->current_name);
        } else {
            return false;
        }
    }

    TypeCheck(IRType _type, const std::string &_curr, const std::string &_out) : Node(_type), current_name(_curr), output_name(_out) {}

    // TODO: this could probably be manually inlined below.
    const std::string get_type_name() const {
        return T::type_name;
    }

    void print(std::ostream &stream, std::string indent) const override {
        const std::string type_name = get_type_name();
        std::string str_cond = Printer::make_type_checker_condition(current_name, type_name, output_name);
        stream << indent << "if (" << str_cond << ") {\n";
        for (const auto &child : children) {
            child->print(stream, indent + "  ");
        }
        stream << indent << "}\n";
    }
};

struct Add final : public TypeCheck<Add> {
    Add(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Add, _curr, _out) {}
    inline static const std::string type_name = "Add";
};

struct Sub final : public TypeCheck<Sub> {
    Sub(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Sub, _curr, _out) {}
    inline static const std::string type_name = "Sub";
};

struct Select final : public TypeCheck<Select> {
    Select(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Select, _curr, _out) {}
    inline static const std::string type_name = "Select";
};

struct IntImm final : public TypeCheck<IntImm> {
    IntImm(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::IntImm, _curr, _out) {}
    inline static const std::string type_name = "IntImm";
};


struct Equality final : public Node {
    std::string name1, name2;

    bool equal(const shared_ptr<Node> &other) const override {
        if (const Equality *other_equal = other->as<Equality>(IRType::Equality)) {
            return (name1 == other_equal->name1) && (name2 == other_equal->name2);
        } else {
            return false;
        }
    }

    Equality(const std::string &_name1, const std::string &_name2) : Node(IRType::Equality), name1(_name1), name2(_name2) {}

    void print(std::ostream &stream, std::string indent) const override {
        stream << indent << "if (equal(" << name1 << ", " << name2 << ")) {\n";
        for (const auto &child : children) {
            child->print(stream, indent + "  ");
        }
        stream << indent << "}\n";
    }
};

struct Return final : public Node {
    std::string retval;

    bool equal(const shared_ptr<Node> &other) const override {
        if (const Return *other_return = other->as<Return>(IRType::Return)) {
            return (retval == other_return->retval);
        } else {
            return false;
        }
    }

    Return(const std::string &_retval) : Node(IRType::Return), retval(_retval) {}

    void print(std::ostream &stream, std::string indent) const override {
        assert(children.empty()); // Return nodes should never have children.
        stream << indent << "return " << retval << ";\n";
    }
};

// Used as a generic condition, makes a lot of stuff easier. Probably could have just inherited from this.
struct Condition final : public Node {
    std::string condition;

    bool equal(const shared_ptr<Node> &other) const override {
        if (const Condition *other_c = other->as<Condition>(IRType::Condition)) {
            return condition == other_c->condition;
        } else {
            return false;
        }
    }

    Condition(const std::string &_condition) : Node(IRType::Condition), condition(_condition) {}

    void print(std::ostream &stream, std::string indent) const override {
        stream << indent << "if (" << condition << ") {\n";
        for (const auto &child : children) {
            child->print(stream, indent + "  ");
        }
        stream << indent << "}\n";
    }
};

// Used as the top level node *ONLY*
struct Sequence final : public Node {

    bool equal(const shared_ptr<Node> &other) const override {
        assert(false); // Should never be compared to other nodes.
    }

    Sequence() : Node(IRType::Sequence) {}

    void print(std::ostream &stream, std::string indent) const override {
        for (const auto &child : children) {
            child->print(stream, indent + "  ");
        }
    }
};

} // namespace Language

// Too many conflicts with Halide IR for other names
using Language::Node;

shared_ptr<Node> tree_constructor(shared_ptr<Node> root, const Expr &expr, const std::string &name, VarScope &scope);

// BinOp is the Halide type, LBinOp is the IR type
template<typename BinOp, typename LBinOp>
inline shared_ptr<Node> handle_bin_op(shared_ptr<Node> &root, const Expr &expr, const std::string &name, VarScope &scope) {
    std::string typed_name = Printer::make_new_unique_name();

    shared_ptr<LBinOp> node = make_shared<LBinOp>(name, typed_name);
    node = root->get_child(node); // Possible replace if it already exists.
    assert(node); // get_child returned the wrong type...?
    typed_name = node->output_name; // This might have changed the name...?

    const BinOp *op = expr.as<BinOp>();
    const std::string a_name = typed_name + "->a";
    const std::string b_name = typed_name + "->b";

    shared_ptr<Node> a_node = tree_constructor(node, op->a, a_name, scope);
    return tree_constructor(a_node, op->b, b_name, scope);
}

shared_ptr<Node> handle_variable(shared_ptr<Node> root, const Variable *var, const std::string &name, VarScope &scope) {
    // TODO: handle constants
    auto iter = scope.find(var->name);
    bool is_const_var = var->name.at(0) == 'c';
    if (iter == scope.end()) {
        scope.insert(std::make_pair(var->name, VarInfo(var->type, name)));
        if (!is_const_var) {
            // Insert into scope and don't worry about it.
            return root;
        } else {
            // TODO: change this to is_const, I am using is_const_v for testing purposes
            const std::string condition = "is_const_v(" + name + ")";
            shared_ptr<Language::Condition> cond_node = make_shared<Language::Condition>(condition);
            return root->get_child(cond_node);
        }
    } else {
        std::string existing_name = iter->second.name;
        shared_ptr<Language::Equality> equal = make_shared<Language::Equality>(existing_name, name);
        shared_ptr<Language::Equality> pequal = root->get_child(equal); // Don't duplicate if possible.
        return pequal;
    }
}

inline shared_ptr<Node> handle_select(shared_ptr<Node> &root, const Expr &expr, const std::string &name, VarScope &scope) {
    std::string typed_name = Printer::make_new_unique_name();

    shared_ptr<Language::Select> node = make_shared<Language::Select>(name, typed_name);
    node = root->get_child(node); // Possible replace if it already exists.
    assert(node); // get_child returned the wrong type...?
    typed_name = node->output_name; // This might have changed the name...?

    const Select *op = expr.as<Select>();
    assert(op); // We failed to identify the Expr properly.
    const std::string cond_name = typed_name + "->condition";
    const std::string true_name = typed_name + "->true_value";
    const std::string false_name = typed_name + "->false_value";

    shared_ptr<Node> cond_node = tree_constructor(node, op->condition, cond_name, scope);
    shared_ptr<Node> true_node = tree_constructor(cond_node, op->true_value, true_name, scope);
    return tree_constructor(true_node, op->false_value, false_name, scope);
}

inline shared_ptr<Node> handle_int_imm(shared_ptr<Node> &root, const IntImm *imm, const std::string &name, VarScope &scope) {
    std::string typed_name = Printer::make_new_unique_name();

    // Inserts the typecheck and fixes name if necessary
    shared_ptr<Language::IntImm> imm_node = make_shared<Language::IntImm>(name, typed_name);
    imm_node = root->get_child(imm_node);
    assert(imm_node);
    typed_name = imm_node->output_name;

    const std::string condition = typed_name + "->value == " + std::to_string(imm->value);
    shared_ptr<Language::Condition> cond_node = make_shared<Language::Condition>(condition);
    // Inserts the condition after the typecheck
    // It seems unlikely that this condition will already exist, but who knows? *shrug*
    return imm_node->get_child(cond_node);
}


template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t) noexcept;

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double) noexcept;

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Add>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    // t.lanes |= ((t.bits >= 32) && add_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return int64_t((uint64_t(a) + uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Add>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a + b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Add>(halide_type_t &t, double a, double b) noexcept {
    return a + b;
}

class ConstantFold : public IRVisitor {
    using IRVisitor::visit;

    void visit(const IntImm *op) override {
        value.u.i64 = op->value;
    }

    void visit(const UIntImm *op) override {
        value.u.u64 = op->value;
    }

    void visit(const FloatImm *op) override {
        value.u.f64 = op->value;
    }

    void visit(const Add *op) override {
        op->a.accept(this);
        auto a = value;
        op->b.accept(this);
        auto b = value;
        Type type = op->type;
        halide_type_t _type = type;
        switch (type.code()) {
            case Type::Int: {
                value.u.i64 = constant_fold_bin_op<Add>(_type, a.u.i64, b.u.i64);
                break;
            }
            case Type::UInt: {
                value.u.u64 = constant_fold_bin_op<Add>(_type, a.u.u64, b.u.u64);
                break;
            }
            case Type::Float:
            case Type::BFloat: {
                value.u.f64 = constant_fold_bin_op<Add>(_type, a.u.f64, b.u.f64);
                break;
            }
            default: {
                // Silent failure. Ask Andrew if we should error out.
                value.u.u64 = 0;
                break;
            }
        }
    }

public:
    halide_scalar_value_t value;

};


Expr fold_actual(const Expr &expr) {
    ConstantFold folder;
    expr.accept(&folder);
    Type type = expr.type();
    switch (type.code()) {
        case Type::Int: {
            return IntImm::make(type, folder.value.u.i64);
        }
        case Type::UInt: {
            return UIntImm::make(type, folder.value.u.u64);
        }
        case Type::Float:
        case Type::BFloat: {
            return FloatImm::make(type, folder.value.u.f64);
        }
        default: {
            assert(false);
            return Expr();
        }
    }
}


/*
TODOs:
    // IntImm,
    UIntImm, // I don't think we will need this, but it's possible
    FloatImm, // or this
    StringImm, // or this
    Broadcast,
    Cast, // not sure about this one, it might make types and stuff difficult. lmk if you see one of these in a rule.
    // Variable,
    // Add,
    // Sub,
    Mod,
    Mul,
    Div,
    Min,
    Max,
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,
    And,
    Or,
    Not,
    // Select,
    Load, // do we actually need this? check simplifier
    Ramp,
    Call, // this will be tricky
    Let, // Don't need this.
    Shuffle, // I don't think we need this right now.
    VectorReduce, // Need this as well
*/
shared_ptr<Node> tree_constructor(shared_ptr<Node> root, const Expr &expr, const std::string &name, VarScope &scope) {
    switch (expr->node_type) {
        case IRNodeType::Sub:
            return handle_bin_op<Halide::Internal::Sub, Language::Sub>(root, expr, name, scope);
        case IRNodeType::Add:
            return handle_bin_op<Halide::Internal::Add, Language::Add>(root, expr, name, scope);
        case IRNodeType::Select:
            return handle_select(root, expr, name, scope);
        // TODO: handle IntImm and the rest
        case IRNodeType::Variable: {
            const Variable *var = expr.as<Variable>();
            return handle_variable(root, var, name, scope);
        }
        case IRNodeType::IntImm: {
            const IntImm *imm = expr.as<IntImm>();
            return handle_int_imm(root, imm, name, scope);
        }
        // case IRNodeType::Call: {
        //     const Call *call = expr.as<Call>();
        //     // Do cases on possible call types
        //     // TODO handle more cases.
        //     if (call->name == "$ramp") {
        //         assert(call->args.size() == 3);
        //         return handle_ramp(root, call->args[0], call->args[1], call->args[2], name, scope);
        //     }
        //     // We don't know what this call is.
        //     std::cerr << "Encountered bad call node in tree_constructor: " << expr << "\n";
        //     assert(false);
        // }
        default:
            assert(false);
    }
    return nullptr;
}

void add_rule(shared_ptr<Node> root, const RewriteRule &rule, const std::string &name) {
    VarScope scope;
    shared_ptr<Node> deepest = tree_constructor(root, rule.before, name, scope);
    if (rule.pred.defined()) {
        // TODO: probably want to assert that child node doesn't exist...?
        const std::string condition = "evaluate_predicate(fold(" + Printer::build_expr(rule.pred, scope) + "))";
        shared_ptr<Language::Condition> cond_node = make_shared<Language::Condition>(condition);
        deepest = deepest->get_child(cond_node);
    }

    const std::string retval = Printer::build_expr(rule.after, scope);
    shared_ptr<Language::Return> ret_node = make_shared<Language::Return>(retval);
    deepest = deepest->get_child(ret_node);
}

shared_ptr<Node> create_graph(const vector<RewriteRule> &rules, const std::string &expr_name) {
    assert(rules.size() > 0);
    
    shared_ptr<Node> root = make_shared<Language::Sequence>();

    for (const auto &rule : rules) {
        add_rule(root, rule, expr_name);
    }
    return root;
}

void print_function(const vector<RewriteRule> &rules, const std::string &func_name, const std::string &expr_name) {
    shared_ptr<Node> root = create_graph(rules, expr_name);

    std::cout << "Expr " << func_name << "(const Expr &" << expr_name << ") {\n";
    root->print(std::cout, "");
    std::cout << "  return " << expr_name << ";\n}\n";
}

// TODO: handle fold somehow...
Expr fold(const Expr &expr) {
    return Call::make(expr.type(), "fold", {expr}, Call::PureIntrinsic);
}

Expr ramp(const Expr &base, const Expr &stride, const Expr &lanes) {
    return Call::make(base.type(), "$ramp", {base, stride, lanes}, Call::PureIntrinsic);
}

// This is for generated code
bool is_const_v(const Expr &expr) {
    if (const Variable *var = expr.as<Variable>()) {
        return var->name.at(0) == 'c';
    } else {
        return is_const(expr);
    }
}


int main(void) {
    Var x("x"), y("y"), z("z"), w("w"), u("u"), v("v");
    Var c0("c0"), c1("c1"), c2("c2"), c3("c3"), c4("c4");

    // had to change select(x, stuff) to select(b0, stuff) for type reasons.
    Expr b0 = Variable::make(UInt(1), "b0");
    
    // TODO: these should be sorted probably.
    // TODO: add some with conditions to check that (probably need more than just Add/Sub/Select)
    vector<RewriteRule> rules = {
      { x - 0, x},
      { select(b0, y, z) - select(b0, w, u), select(b0, y - w, z - u) },
      { select(b0, y, z) - y, select(b0, 0, z - y) },
      { select(b0, y, z) - z, select(b0, y - z, 0) },
      { y - select(b0, y, z), select(b0, 0, y - z) },
      { z - select(b0, y, z), select(b0, z - y, 0) },

      { select(b0, y + w, z) - y, select(b0, w, z - y) },
      { select(b0, w + y, z) - y, select(b0, w, z - y) },
      { select(b0, y, z + w) - z, select(b0, y - z, w) },
      { select(b0, y, w + z) - z, select(b0, y - z, w) },
      { y - select(b0, y + w, z), 0 - select(b0, w, z - y) },
      { y - select(b0, w + y, z), 0 - select(b0, w, z - y) },
      { z - select(b0, y, z + w), 0 - select(b0, y - z, w) },
      { z - select(b0, y, w + z), 0 - select(b0, y - z, w) },

      { (x + y) - x, y },
      { (x + y) - y, x },
      { x - (x + y), -y },
      { y - (x + y), -x },
      { (x - y) - x, -y },
      { (select(b0, y, z) + w) - select(b0, u, v), select(b0, y - u, z - v) + w },
      { (w + select(b0, y, z)) - select(b0, u, v), select(b0, y - u, z - v) + w },
      { select(b0, y, z) - (select(b0, u, v) + w), select(b0, y - u, z - v) - w },
      { select(b0, y, z) - (w + select(b0, u, v)), select(b0, y - u, z - v) - w },
      { (select(b0, y, z) - w) - select(b0, u, v), select(b0, y - u, z - v) - w },
      { c0 - select(b0, c1, c2), select(b0, fold(c0 - c1), fold(c0 - c2)) },
      { (x + c0) - c1, x + fold(c0 - c1) },
      { (x + c0) - (c1 - y), (x + y) + fold(c0 - c1) },
      { (x + c0) - (y + c1), (x - y) + fold(c0 - c1) },
      { (x + c0) - y, (x - y) + c0 },
      { (c0 - x) - (c1 - y), (y - x) + fold(c0 - c1) },
      { (c0 - x) - (y + c1), fold(c0 - c1) - (x + y) },
      { x - (y - z), x + (z - y) },
      // This rule below screws things up for one of the rules above
      { x - (y + c0), (x - y) - c0 },
      { (c0 - x) - c1, fold(c0 - c1) - x },
    };

    print_function(rules, "simplify_sub", "expr");

    // this is for checking correctness, uncomment out when checking.

    /*
    for (const auto &rule : rules) {
        Expr simpl = simplify_sub(rule.before);
        std::cerr << "Original: " << rule.before << "\n";
        std::cerr << simpl << " vs. " << rule.after << "\n";
        if (!equal(simpl, rule.after)) {
            std::cerr << "ERROR\n";
        }
    }
    */


    // For testing a single rule (debugging codegen)
    /*
    Expr expr = ((c0 - x) - (y + c1));
    Expr expected = (fold(c0 - c1) - (x + y));
    Expr simpl = simplify_sub(expr);
    std::cerr << expr << " -> " << simpl << "\n";
    std::cerr << "Expected: " << expected << "\n";
    */


    return 0;
}
