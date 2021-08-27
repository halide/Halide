#include "Halide.h"
#include "rewrite_rule.h"
#include "merge_rules.h"
#include "single_rule.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>

using namespace Halide;
using namespace Halide::Internal;

typedef std::map<std::string, std::string> VarScope;


using std::shared_ptr;
using std::make_shared;
using std::vector;
using namespace std;
static int name_cnt = 0;

std::string make_type_check_condition(std::string var_name, std::string type_name, std::string output_name) {
    return "const " + type_name + " *" + output_name + " = " + var_name + ".as<" + type_name + ">()";
}

std::string build_return_stmt(const Expr &expr, const VarScope &scope) {
    if (const Add *op = expr.as<Add>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " + " + b + ")";
    } else if (const Mul *op = expr.as<Mul>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " * " + b + ")";
    } else if (const Sub *op = expr.as<Sub>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " - " + b + ")";
    } else if (const Div *op = expr.as<Div>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " / " + b + ")";
    } else if (const Mod *op = expr.as<Mod>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " % " + b + ")";
    } else if (const Min *op = expr.as<Min>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "min(" + a + ", " + b + ")";
    } else if (const Max *op = expr.as<Max>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "max(" + a + ", " + b + ")";
    } else if (const EQ *op = expr.as<EQ>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " == " + b + ")";
    } else if (const NE *op = expr.as<NE>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " != " + b + ")";
    } else if (const LT *op = expr.as<LT>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " < " + b + ")";
    } else if (const LE *op = expr.as<LE>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " <= " + b + ")";
    } else if (const GT *op = expr.as<GT>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " > " + b + ")";
    } else if (const GE *op = expr.as<GE>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " >= " + b + ")";
    } else if (const And *op = expr.as<And>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " && " + b + ")";
    } else if (const Or *op = expr.as<Or>()) {
        std::string a = build_return_stmt(op->a, scope);
        std::string b = build_return_stmt(op->b, scope);
        return "(" + a + " || " + b + ")";
    } else if (const Not *op = expr.as<Not>()) {
        std::string a = build_return_stmt(op->a, scope);
        return "(!" + a + ")";
    } else if (const Select *op = expr.as<Select>()) {
        std::string c = build_return_stmt(op->condition, scope);
        std::string t = build_return_stmt(op->true_value, scope);
        std::string f = build_return_stmt(op->false_value, scope);
        return "select(" + c + ", " + t + ", " + f + ")";
    } else if (const Broadcast *op = expr.as<Broadcast>()) {
        std::string v = build_return_stmt(op->value, scope);
        std::string l = build_return_stmt(op->lanes, scope);
        return "broadcast(" + v + ", " + l + ")";
    } else if (const Ramp *op = expr.as<Ramp>()) {
        std::string b = build_return_stmt(op->base, scope);
        std::string s = build_return_stmt(op->stride, scope);
        std::string l = build_return_stmt(op->lanes, scope);
        return "ramp(" + b + ", " + s + ", " + l + ")";
    } else if (const Variable *op = expr.as<Variable>()) {
        auto iter = scope.find(op->name);
        assert(iter != scope.end()); // TODO: if built inside Halide main code, use internal_assert or user_assert
        return iter->second;
    }  else if (const IntImm *op = expr.as<IntImm>()) {
        return std::to_string(op->value);
    } else {
        std::cerr << expr << "\n";
        assert(false);
        return "";
    }
    // TODO: add a bunch more cases
}


std::ostream& operator << (std::ostream& os, const IRNodeType& t) {
    switch (t) {
        case IRNodeType::Add: {
            os << "Add";
            break;
        }
        case IRNodeType::Sub: {
            os << "Sub";
            break;
        }
        case IRNodeType::Variable: {
            os << "Variable";
            break;
        }
        default: {
            os << "ERROR";
        }
    }
    return os;
}

std::string get_type_name(const IRNodeType& t) {
    switch (t) {
        case IRNodeType::Add: {
            return "Add";
        }
        case IRNodeType::Sub: {
            return "Sub";
        }
        case IRNodeType::Variable: {
            return "Variable";
        }
        default: {
            return "ERROR";
        }
    }
}

struct Node {
    // TODO: what do we need??
    Halide::Internal::IRNodeType ir_node_type;
    std::string op_name;
    std::string cur_name;
    std::pair<std::string,std::string> eq_names;
    vector<shared_ptr<Node>> children;
    Node(Halide::Internal::IRNodeType type) : ir_node_type(type) {}
    // Constructs a child node if one does not exist
    // TODO: do we need more info than type?
    shared_ptr<Node> get_child(IRNodeType type, const std::string& _cur) {
        auto is_node = [&type](const shared_ptr<Node> &child) { return child->ir_node_type == type; };
        auto result = std::find_if(children.begin(), children.end(), is_node);
        if (result != children.end()) {
            return *result;
        } else {
            // Need to create child.
            shared_ptr<Node> child = make_shared<Node>(type);
            child->op_name = generate_unique_name();
            child->cur_name = _cur;
            children.push_back(child);
            return child;
        }
    }
    void print(std::string indent = "") {
        std::cout << indent << ir_node_type << " " << op_name << " {\n";
        for (const auto &child : children) {
            child->print(indent + "  ");
        }
        std::cout << indent << "} \n";
    }
    std::string generate_unique_name() {
        return "t" + std::to_string(name_cnt++);
    }
};

template<typename BinaryOp>
inline shared_ptr<Node> insert_binary_op(shared_ptr<Node> &root, const Expr rule, const std::string &name, VarScope &mutable_scope) {
    const BinaryOp *op = rule.as<BinaryOp>();
    const std::string a_name = name + "->a";
    const std::string b_name = name + "->b";
    if(op->a.node_type() == IRNodeType::Variable) {

        const Variable *var_a = op->a.template as<Variable>();
        auto iter = mutable_scope.find(var_a->name);
        if(iter != mutable_scope.end()) {
            bool create_new_equality_a = true;
            shared_ptr<Node> eq_node_a = nullptr;
            for(auto c : root->children) {
                if(c->ir_node_type == IRNodeType::Variable && c->op_name == "equal" && c->eq_names.first == a_name && c->eq_names.second == iter->second) {
                    create_new_equality_a = false;
                    eq_node_a = c;
                }
            }
            if(create_new_equality_a) {
                shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                child->op_name = "equal";
                child->eq_names = {a_name, iter->second};
                root->children.push_back(child);
                eq_node_a = child;
            } 
                
            if(op->b.node_type() == IRNodeType::Variable) {
                const Variable *var_b = op->b.template as<Variable>();
                auto iter = mutable_scope.find(var_b->name);
                if(iter != mutable_scope.end()) {
                    bool create_new_equality_b = true;
                    shared_ptr<Node> eq_node_b = nullptr;
                    for(auto c : eq_node_a->children) {
                        if(c->ir_node_type == IRNodeType::Variable && c->op_name == "equal" && c->eq_names.first == a_name && c->eq_names.second == iter->second) {
                            create_new_equality_b = false;
                            eq_node_b = c;
                        }
                    } 
                    if(create_new_equality_b) {
                        shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                        child->op_name = "equal";
                        child->eq_names = {b_name, iter->second};
                        eq_node_a->children.push_back(child);
                        eq_node_b = child;
                    }
                    shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                    child->op_name = "return"; 
                    eq_node_b->children.push_back(child); 
                    return child;
                } else {
                    shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                    child->op_name = "return"; 
                    eq_node_a->children.push_back(child);
                    mutable_scope[var_b->name] = b_name;
                    return child;
                }
            } else {
                shared_ptr<Node> res = root->get_child(op->b.node_type(), b_name);
                return recursive_insert_rule(res, op->b, res->op_name, mutable_scope);
            } 
        } else {
            mutable_scope[var_a->name] = a_name;
            if(op->b.node_type() == IRNodeType::Variable) {
                const Variable *var_b = op->b.template as<Variable>();
                auto iter = mutable_scope.find(var_b->name);
                if(iter != mutable_scope.end()) {
                    bool create_new_equality_b = true;
                    shared_ptr<Node> eq_node_b = nullptr;
                    for(auto c : root->children) {
                        if(c->ir_node_type == IRNodeType::Variable && c->op_name == "equal" && c->eq_names.first == a_name && c->eq_names.second == iter->second) {
                            create_new_equality_b = false;
                            eq_node_b = c;
                        }
                    } 
                    if(create_new_equality_b) {
                        shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                        child->op_name = "equal";
                        child->eq_names = {b_name, iter->second};
                        root->children.push_back(child);
                        eq_node_b = child;
                    }
                    shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                    child->op_name = "return"; 
                    eq_node_b->children.push_back(child);
                    return child;
                } else {
                    mutable_scope[var_b->name] = b_name;
                    return root;
                }
            } else {
                shared_ptr<Node> res = root->get_child(op->b.node_type(), b_name);
                return recursive_insert_rule(res, op->b, res->op_name, mutable_scope);
            }
        }

    } else {
        shared_ptr<Node> res = root->get_child(op->a.node_type(), a_name);
        shared_ptr<Node> a_node = recursive_insert_rule(res, op->a, res->op_name, mutable_scope);
        if(op->b.node_type() == IRNodeType::Variable) {
            const Variable *var = op->b.template as<Variable>();
            auto iter = mutable_scope.find(var->name);
            if(iter != mutable_scope.end()) {
                bool create_new_equality_b = true;
                shared_ptr<Node> eq_node_b = nullptr;
                for(auto c : a_node->children) {
                    if(c->ir_node_type == IRNodeType::Variable && c->op_name == "equal" && c->eq_names.first == a_name && c->eq_names.second == iter->second) {
                        create_new_equality_b = false;
                        eq_node_b = c;
                    }
                } 
                if(create_new_equality_b) {
                    shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                    child->op_name = "equal";
                    child->eq_names = {b_name, iter->second};
                    a_node->children.push_back(child);
                    eq_node_b = child;
                }
                shared_ptr<Node> child = make_shared<Node>(IRNodeType::Variable);
                child->op_name = "return"; 
                eq_node_b->children.push_back(child);
                return child;
            } else {
                mutable_scope[var->name] = b_name;
                return a_node;
            }
        } else {
            shared_ptr<Node> res = a_node->get_child(op->b.node_type(), b_name);
            return recursive_insert_rule(res, op->b, res->op_name, mutable_scope);
        }
    } 
}

shared_ptr<Node> recursive_insert_rule(shared_ptr<Node> root, const Expr rule, const std::string &name, VarScope &mutable_scope) {
    assert (rule.node_type() == root->ir_node_type);
    switch (root->ir_node_type) {
        case IRNodeType::Sub:
            return insert_binary_op<Sub>(root, rule, name, mutable_scope);
        case IRNodeType::Add:
            return insert_binary_op<Add>(root, rule, name, mutable_scope);
        // case IRNodeType::Variable: {
        //     return root;
        // }
        default:
            assert(false);
    }
}

void insert_rule(shared_ptr<Node> &root, RewriteRule rule) {
    VarScope mutable_scope;
    shared_ptr<Node> res = recursive_insert_rule(root, rule.before, root->op_name, mutable_scope);
    res->cur_name = build_return_stmt(rule.after, mutable_scope);
}

shared_ptr<Node> create_graph(vector<RewriteRule> rules) {
    assert(rules.size() > 0);
    // TODO: extract the top level node type
    Halide::Internal::IRNodeType type = rules[0].before.node_type();
    // TODO: assert that all rules' before are the same type
    shared_ptr<Node> root = make_shared<Node>(type);
    root->op_name = root->generate_unique_name();
    root->cur_name = "expr";
    for (const auto &rule : rules) {
        insert_rule(root, rule);
    }
    return root;
}

void printTree(std::shared_ptr<Node> root, std::string indent = "") {
    if(root->ir_node_type == IRNodeType::Variable) {
        if(root->op_name == "equal") {
            std::cout << indent << "if (equal(" << root->eq_names.first << ", " << root->eq_names.second << ")) {\n"; 
        } else {
            std::cout << indent << "return " << root->cur_name << ";\n"; 
            return;
        }
    } else {
        std::cout << indent << "if (" << make_type_check_condition(root->cur_name, get_type_name(root->ir_node_type), root->op_name) << ") {\n";
    }
    for (const auto &child : root->children) {
        printTree(child, indent + "    ");
    }
    std::cout << indent << "} \n";
}

std::string merge_rules_function(const std::vector<RewriteRule> &rules, std::string func_name, std::string var_name) {
    std::ostringstream stream;
    stream << "Expr " << func_name << "(const Expr &" << var_name << ") {\n";
    std::shared_ptr<Node> root = create_graph(rules);
    //root->print();
    printTree(root);    
    stream << "\treturn " << var_name << ";\n}\n";
    return stream.str();
}