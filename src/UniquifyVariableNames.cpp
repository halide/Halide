#include "UniquifyVariableNames.h"
#include "IRMutator.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::map;
using std::string;

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

    template<typename T>
    auto visit_let(const T *op) -> decltype(op->body) {
        using Body = decltype(op->body);
        struct Frame {
            const T *op;
            UniquifyVariableNames *u;
            Body *result;
            Expr value;
            string new_name;

            Frame(const T *op, UniquifyVariableNames *u, Body *result) : op(op), u(u), result(result) {
                value = u->mutate(op->value);
                u->push_name(op->name);
                new_name = u->get_name(op->name);
            }

            ~Frame() {
                if (!value.defined()) return; // Was moved-from in vector realloc

                u->pop_name(op->name);
                if (new_name == op->name &&
                    result->same_as(op->body) &&
                    value.same_as(op->value)) {
                    *result = op;
                } else {
                    *result = T::make(new_name, value, *result);
                }
            }

            Frame(const Frame &) = delete;
            Frame(Frame &&) = default;
        };

        Body result;

        {
            std::vector<Frame> stack;
            stack.emplace_back(op, this, &result);
            Body body = op->body;
            while (const T *t = body.template as<T>()) {
                stack.emplace_back(t, this, &result);
                body = t->body;
            }
            result = mutate(body);
        }

        // The destructors of the Frame objects have rewrapped the
        // result with the appropriate lets
        return result;
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
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

}  // namespace Internal
}  // namespace Halide
