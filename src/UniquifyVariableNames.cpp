#include "UniquifyVariableNames.h"
#include "IRMutator.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::string;
using std::map;

class UniquifyVariableNames : public IRMutator2 {

    using IRMutator2::visit;

    map<string, int> vars;

    void push_name(string s) {
        if (vars.find(s) == vars.end()) {
            vars[s] = 0;
        } else {
            vars[s]++;
        }
    }

    string get_name(string s) {
        if (vars.find(s) == vars.end()) {
            return s;
        } else if (vars[s] == 0) {
            return s;
        } else {
            std::ostringstream oss;
            oss << s << "_" << vars[s];
            return oss.str();
        }
    }

    void pop_name(string s) {
        vars[s]--;
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        push_name(op->name);
        string new_name = get_name(op->name);
        Stmt body = mutate(op->body);
        pop_name(op->name);

        if (new_name == op->name &&
            body.same_as(op->body) &&
            value.same_as(op->value)) {
            return op;
        } else {
            return LetStmt::make(new_name, value, body);
        }

    }

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        push_name(op->name);
        string new_name = get_name(op->name);
        Expr body = mutate(op->body);
        pop_name(op->name);

        if (new_name == op->name &&
            body.same_as(op->body) &&
            value.same_as(op->value)) {
            return op;
        } else {
            return Let::make(new_name, value, body);
        }

    }

    Stmt visit(const For *op) override {
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        push_name(op->name);
        string new_name = get_name(op->name);
        Stmt body = mutate(op->body);
        pop_name(op->name);

        if (new_name == op->name &&
            body.same_as(op->body) &&
            min.same_as(op->min) &&
            extent.same_as(op->extent)) {
            return op;
        } else {
            return For::make(new_name, min, extent, op->for_type, op->device_api, body);
        }
    }

    Expr visit(const Variable *op) override {
        string new_name = get_name(op->name);
        if (op->name != new_name) {
            return Variable::make(op->type, new_name);
        } else {
            return op;
        }
    }
};

Stmt uniquify_variable_names(Stmt s) {
    UniquifyVariableNames u;
    return u.mutate(s);
}

}
}
