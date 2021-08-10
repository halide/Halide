#include <iostream>
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

typedef std::map<std::string, std::string> VarScope;

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
    return "t" + std::to_string(counter++);
}

std::string build_expr(const Expr &expr, const VarScope &scope);

std::string make_inner_binary_op(const Expr &a, const Expr &b, const VarScope &scope, const std::string &symbol) {
    std::string as = build_expr(a, scope);
    std::string bs = build_expr(b, scope);
    return "(" + as + " " + symbol + " " + bs + ")";
}

std::string make_outer_binary_op(const Expr &a, const Expr &b, const VarScope &scope, const std::string &symbol) {
    std::string as = build_expr(a, scope);
    std::string bs = build_expr(b, scope);
    return symbol + "(" + as + " , " + bs + ")";
}

std::string build_expr(const Expr &expr, const VarScope &scope) {
    if (const Add *op = expr.as<Add>()) {
        return make_inner_binary_op(op->a, op->b, scope, "+");
    } else if (const Mul *op = expr.as<Mul>()) {
        return make_inner_binary_op(op->a, op->b, scope, "*");
    } else if (const Sub *op = expr.as<Sub>()) {
        return make_inner_binary_op(op->a, op->b, scope, "-");
    } else if (const Div *op = expr.as<Div>()) {
        return make_inner_binary_op(op->a, op->b, scope, "/");
    } else if (const Mod *op = expr.as<Mod>()) {
        return make_inner_binary_op(op->a, op->b, scope, "%");
    } else if (const Min *op = expr.as<Min>()) {
        return make_outer_binary_op(op->a, op->b, scope, "min");
    } else if (const Max *op = expr.as<Max>()) {
        return make_outer_binary_op(op->a, op->b, scope, "max");
    } else if (const EQ *op = expr.as<EQ>()) {
        return make_inner_binary_op(op->a, op->b, scope, "==");
    } else if (const NE *op = expr.as<NE>()) {
        return make_inner_binary_op(op->a, op->b, scope, "!=");
    } else if (const LT *op = expr.as<LT>()) {
        return make_inner_binary_op(op->a, op->b, scope, "<");
    } else if (const LE *op = expr.as<LE>()) {
        return make_inner_binary_op(op->a, op->b, scope, "<=");
    } else if (const GT *op = expr.as<GT>()) {
        return make_inner_binary_op(op->a, op->b, scope, ">");
    } else if (const GE *op = expr.as<GE>()) {
        return make_inner_binary_op(op->a, op->b, scope, ">=");
    } else if (const And *op = expr.as<And>()) {
        return make_inner_binary_op(op->a, op->b, scope, "&&");
    } else if (const Or *op = expr.as<Or>()) {
        return make_inner_binary_op(op->a, op->b, scope, "||");
    } else if (const Not *op = expr.as<Not>()) {
        std::string a = build_expr(op->a, scope);
        return "(!" + a + ")";
    } else if (const Select *op = expr.as<Select>()) {
        std::string c = build_expr(op->condition, scope);
        std::string t = build_expr(op->true_value, scope);
        std::string f = build_expr(op->false_value, scope);
        return "select(" + c + ", " + t + ", " + f + ")";
    } else if (const Broadcast *op = expr.as<Broadcast>()) {
        std::string v = build_expr(op->value, scope);
        std::string l = build_expr(op->lanes, scope);
        return "broadcast(" + v + ", " + l + ")";
    } else if (const Ramp *op = expr.as<Ramp>()) {
        std::string b = build_expr(op->base, scope);
        std::string s = build_expr(op->stride, scope);
        std::string l = build_expr(op->lanes, scope);
        return "ramp(" + b + ", " + s + ", " + l + ")";
    } else if (const Variable *op = expr.as<Variable>()) {
        auto iter = scope.find(op->name);
        assert(iter != scope.end()); // TODO: if built inside Halide main code, use internal_assert or user_assert
        return iter->second;
    }  else if (const IntImm *op = expr.as<IntImm>()) {
        return std::to_string(op->value);
    }  else if (const Call *op = expr.as<Call>()) {
        if (op->name == "fold") {
            assert(op->args.size() == 1);
            const std::string arg = build_expr(op->args[0], scope);
            return "fold(" + arg + ")";
        } else {
            // What else could it be?
            std::cerr << "Unknown Call Expr:" << expr << "\n";
            assert(false);
        }
    } else {
        std::cerr << expr << "\n";
        assert(false);
        return "";
    }
    // TODO: add a bunch more cases
}

} // namespace Printer


namespace Language {


enum class IRType {
    // Type checks
    Add, Sub, Select,
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

     std::string get_type_name() const {
        // TODO: make this statically defined by the value of T.
        switch (type) {
            case IRType::Add: {
                return "Add";
            }
            case IRType::Sub: {
                return "Sub";
            }
            case IRType::Select: {
                return "Select";
            }
            // TODO: Do the rest
            default: {
                assert(false); // Should have implemented all TC types above.
                return "ERROR";
            }
        }
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
};

struct Sub final : public TypeCheck<Sub> {
    Sub(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Sub, _curr, _out) {}
};

struct Select final : public TypeCheck<Select> {
    Select(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Select, _curr, _out) {}
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

// Used as the top level node
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
        scope[var->name] = name;
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
        std::string existing_name = iter->second;
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

/*
TODOs:
    IntImm,
    UIntImm,
    FloatImm,
    StringImm,
    Broadcast,
    Cast,
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
    Let, // probably don't
    Shuffle, // not sure about this one
    VectorReduce,
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
            // TODO: probably need to do something else here?
            const Variable *var = expr.as<Variable>();
            return handle_variable(root, var, name, scope);
        }
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

// This is for generated code
bool is_const_v(const Expr &expr) {
    if (const Variable *var = expr.as<Variable>()) {
        return var->name.at(0) == 'c';
    } else {
        return is_const(expr);
    }
}

/*
This was generated from the rules list in main() below.
*/
Expr simplify_sub(const Expr &expr) {
  if (const Sub *t0 = expr.as<Sub>()) {
    if (const Select *t1 = t0->a.as<Select>()) {
      if (const Select *t2 = t0->b.as<Select>()) {
        if (equal(t1->condition, t2->condition)) {
          return select(t1->condition, (t1->true_value - t2->true_value), (t1->false_value - t2->false_value));
        }
      }
      if (equal(t1->true_value, t0->b)) {
        return select(t1->condition, 0, (t1->false_value - t1->true_value));
      }
      if (equal(t1->false_value, t0->b)) {
        return select(t1->condition, (t1->true_value - t1->false_value), 0);
      }
      if (const Add *t13 = t1->true_value.as<Add>()) {
        if (equal(t13->a, t0->b)) {
          return select(t1->condition, t13->b, (t1->false_value - t13->a));
        }
        if (equal(t13->b, t0->b)) {
          return select(t1->condition, t13->a, (t1->false_value - t13->b));
        }
      }
      if (const Add *t19 = t1->false_value.as<Add>()) {
        if (equal(t19->a, t0->b)) {
          return select(t1->condition, (t1->true_value - t19->a), t19->b);
        }
        if (equal(t19->b, t0->b)) {
          return select(t1->condition, (t1->true_value - t19->b), t19->a);
        }
      }
      if (const Add *t55 = t0->b.as<Add>()) {
        if (const Select *t56 = t55->a.as<Select>()) {
          if (equal(t1->condition, t56->condition)) {
            return (select(t1->condition, (t1->true_value - t56->true_value), (t1->false_value - t56->false_value)) - t55->b);
          }
        }
        if (const Select *t60 = t55->b.as<Select>()) {
          if (equal(t1->condition, t60->condition)) {
            return (select(t1->condition, (t1->true_value - t60->true_value), (t1->false_value - t60->false_value)) - t55->a);
          }
        }
      }
    }
    if (const Select *t8 = t0->b.as<Select>()) {
      if (equal(t0->a, t8->true_value)) {
        return select(t8->condition, 0, (t0->a - t8->false_value));
      }
      if (equal(t0->a, t8->false_value)) {
        return select(t8->condition, (t0->a - t8->true_value), 0);
      }
      if (const Add *t25 = t8->true_value.as<Add>()) {
        if (equal(t0->a, t25->a)) {
          return (0 - select(t8->condition, t25->b, (t8->false_value - t0->a)));
        }
        if (equal(t0->a, t25->b)) {
          return (0 - select(t8->condition, t25->a, (t8->false_value - t0->a)));
        }
      }
      if (const Add *t31 = t8->false_value.as<Add>()) {
        if (equal(t0->a, t31->a)) {
          return (0 - select(t8->condition, (t8->true_value - t0->a), t31->b));
        }
        if (equal(t0->a, t31->b)) {
          return (0 - select(t8->condition, (t8->true_value - t0->a), t31->a));
        }
      }
    }
    if (const Add *t36 = t0->a.as<Add>()) {
      if (equal(t36->a, t0->b)) {
        return t36->b;
      }
      if (equal(t36->b, t0->b)) {
        return t36->a;
      }
      if (const Select *t47 = t36->a.as<Select>()) {
        if (const Select *t48 = t0->b.as<Select>()) {
          if (equal(t47->condition, t48->condition)) {
            return (select(t47->condition, (t47->true_value - t48->true_value), (t47->false_value - t48->false_value)) + t36->b);
          }
        }
      }
      if (const Select *t51 = t36->b.as<Select>()) {
        if (const Select *t52 = t0->b.as<Select>()) {
          if (equal(t51->condition, t52->condition)) {
            return (select(t51->condition, (t51->true_value - t52->true_value), (t51->false_value - t52->false_value)) + t36->a);
          }
        }
      }
      if (is_const_v(t36->b)) {
        if (is_const_v(t0->b)) {
          return (t36->a + fold((t36->b - t0->b)));
        }
        if (const Sub *t71 = t0->b.as<Sub>()) {
          if (is_const_v(t71->a)) {
            return ((t36->a + t71->b) + fold((t36->b - t71->a)));
          }
        }
        if (const Add *t74 = t0->b.as<Add>()) {
          if (is_const_v(t74->b)) {
            return ((t36->a - t74->a) + fold((t36->b - t74->b)));
          }
        }
        return ((t36->a - t0->b) + t36->b);
      }
    }
    if (const Add *t40 = t0->b.as<Add>()) {
      if (equal(t0->a, t40->a)) {
        return (0 - t40->b);
      }
      if (equal(t0->a, t40->b)) {
        return (0 - t40->a);
      }
      if (is_const_v(t40->b)) {
        return ((t0->a - t40->a) - t40->b);
      }
    }
    if (const Sub *t44 = t0->a.as<Sub>()) {
      if (equal(t44->a, t0->b)) {
        return (0 - t44->b);
      }
      if (const Select *t63 = t44->a.as<Select>()) {
        if (const Select *t64 = t0->b.as<Select>()) {
          if (equal(t63->condition, t64->condition)) {
            return (select(t63->condition, (t63->true_value - t64->true_value), (t63->false_value - t64->false_value)) - t44->b);
          }
        }
      }
      if (is_const_v(t44->a)) {
        if (const Sub *t79 = t0->b.as<Sub>()) {
          if (is_const_v(t79->a)) {
            return ((t79->b - t44->b) + fold((t44->a - t79->a)));
          }
        }
        if (const Add *t82 = t0->b.as<Add>()) {
          if (is_const_v(t82->b)) {
            return (fold((t44->a - t82->b)) - (t44->b + t82->a));
          }
        }
        if (is_const_v(t0->b)) {
          return (fold((t44->a - t0->b)) - t44->b);
        }
      }
    }
    if (is_const_v(t0->a)) {
      if (const Select *t66 = t0->b.as<Select>()) {
        if (is_const_v(t66->true_value)) {
          if (is_const_v(t66->false_value)) {
            return select(t66->condition, fold((t0->a - t66->true_value)), fold((t0->a - t66->false_value)));
          }
        }
      }
    }
    if (const Sub *t84 = t0->b.as<Sub>()) {
      return (t0->a + (t84->b - t84->a));
    }
  }
  return expr;
}

int main(void) {
    Var x("x"), y("y"), z("z"), w("w"), u("u"), v("v");
    Var c0("c0"), c1("c1"), c2("c2"), c3("c3"), c4("c4");

    // had to change select(x, stuff) to select(b0, stuff) for type reasons.
    Expr b0 = Variable::make(UInt(1), "b0");
    
    // TODO: these should be sorted probably.
    // TODO: add some with conditions to check that (probably need more than just Add/Sub/Select)
    vector<RewriteRule> rules = {
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
      // This rule below scres things up for one of the rules above
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
