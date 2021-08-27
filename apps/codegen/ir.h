#ifndef HL_IR_H
#define HL_IR_H
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <assert.h>


using namespace std;

enum class IRType {
    // Expr
    Add, Sub, Div, Mul, IntImm, Var,
    // TODO: add the rest
    
    // Stmt
    Equality, Return,
};

struct Node {
    template<typename Type>
    shared_ptr<Node> get_child(IRType type, const std::string& _out);
    virtual void print(std::ostream &stream, std::string indent) const = 0; // This makes the struct abstract.
    virtual bool equal(const shared_ptr<Node> &other) const = 0;
    std::string generate_name() { return "t" + std::to_string(name_cnt++); }
    virtual ~Node() = default; 

    vector<shared_ptr<Node>> children;
    IRType type;
    std::string current_name;
    std::string output_name;
    int name_cnt;

    Node(IRType _type) : type(_type), name_cnt{0} {}
    Node(IRType _type, const std::string &_curr, const std::string &_out) : type(_type), current_name(_curr), output_name(_out), name_cnt{0} {}

    template<typename T>
    const T *as(IRType _type) const {
        if (type != _type) {
            return nullptr;
        } else {
            return dynamic_cast<const T*>(this);
        }

    }
};

std::string make_type_check_condition(const std::string &var_name, const std::string &type_name, const std::string &output_name) {
    return "const " + type_name + " *" + output_name + " = " + var_name + ".as<" + type_name + ">()";
}

struct Add_Node;

template<typename T>
struct TypeCheck : public Node {
    // std::string current_name;
    // std::string output_name;

    template<typename Type>
    shared_ptr<Node> get_child(IRType type, const std::string& _out) {
        auto is_node = [&type](const shared_ptr<Node> &child) { return child->type == type; };
        auto result = std::find_if(children.begin(), children.end(), is_node);
        if (result != children.end()) {
            return *result;
        } else {
            // Need to create child.
             shared_ptr<Node> child = make_shared<TypeCheck<Type>>(type, generate_name(), _out);
             children.push_back(child);
            return nullptr;
        }
    }

    bool equal(const shared_ptr<Node>& other) const override {
        // TODO: need to cast to TypeCheck node
        // return (type == other->type) && (current_name == other->current_name) && (output_name == other->output_name);
        if (const T *other_tc = other->as<T>(type)) {
            return (current_name == other_tc->current_name) && (output_name == other_tc->output_name);
        } else {
            return false;
        }
    }

    TypeCheck(IRType _type, const std::string &_curr, const std::string &_out) : Node(_type, _curr, _out) {}

     std::string get_type_name() const {
        switch (type) {
            case IRType::Add: {
                return "Add";
            }
            case IRType::Sub: {
                return "Sub";
            }
            case IRType::Div: {
                return "Div";
            }
            case IRType::Mul: {
                return "Mul";
            }
            // Do the rest
            default: {
                return "ERROR";
            }
        }
    }

    void print(std::ostream &stream, std::string indent) const override {
        const std::string type_name = get_type_name();
        std::string str_cond = make_type_check_condition(current_name, type_name, output_name);
        stream << indent << "if (" << str_cond << ") {\n";
        for (const auto &child : children) {
            child->print(stream, indent + "  ");
        }
        stream << indent << "}\n";
    }

};

struct Add_Node final : public TypeCheck<Add_Node> {
    Add_Node(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Add, _curr, _out) {}
};

struct Sub_Node final : public TypeCheck<Sub_Node> {
    Sub_Node(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Sub, _curr, _out) {}
};

// struct Div final : public TypeCheck<Div> {
//     Div(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Div, _curr, _out) {}
// };

// struct Mul final : public TypeCheck<Mul> {
//     Mul(const std::string &_curr, const std::string &_out) : TypeCheck(IRType::Mul, _curr, _out) {}
// };

struct Equality final : public Node {
    std::string name1, name2;

    shared_ptr<Node> get_child(IRType type, const std::string& _out) {
       return nullptr; 
    }
    
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

    shared_ptr<Node> get_child(IRType type, const std::string& _out) {
        return nullptr;
    }

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

#endif // HL_IR_H
