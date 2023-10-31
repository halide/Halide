#include "UniquifyVariableNames.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Var.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::pair;
using std::string;
using std::vector;

namespace {
class UniquifyVariableNames : public IRMutator {

    using IRMutator::visit;

    // The mapping from old names to new names
    Scope<string> renaming;

    // Get a new previously unused name for a let binding or for loop,
    // and push it onto the renaming. Will return the original name if
    // possible, but pushes unconditionally to simplify cleanup.
    string make_new_name(const string &base) {
        if (!renaming.contains(base)) {
            renaming.push(base, base);
            return base;
        }
        for (size_t i = std::max((size_t)1, renaming.count(base));; i++) {
            string candidate = base + "_" + std::to_string(i);
            if (!renaming.contains(candidate)) {
                // Reserve this name for this base name
                renaming.push(base, candidate);
                // And reserve the generated name forever more (will not be popped)
                renaming.push(candidate, candidate);
                return candidate;
            }
        }
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        struct Frame {
            const LetOrLetStmt *op;
            Expr value;
            string new_name;
        };

        vector<Frame> frames;
        decltype(op->body) result;
        while (op) {
            frames.emplace_back();
            auto &f = frames.back();
            f.op = op;
            f.value = mutate(op->value);
            f.new_name = make_new_name(op->name);
            result = op->body;
            op = result.template as<LetOrLetStmt>();
        }

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            renaming.pop(it->op->name);
            if (it->new_name == it->op->name &&
                result.same_as(it->op->body) &&
                it->op->value.same_as(it->value)) {
                result = it->op;
            } else {
                result = LetOrLetStmt::make(it->new_name, it->value, result);
            }
        }

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
        string new_name = make_new_name(op->name);
        Stmt body = mutate(op->body);
        renaming.pop(op->name);

        if (new_name == op->name &&
            body.same_as(op->body) &&
            min.same_as(op->min) &&
            extent.same_as(op->extent)) {
            return op;
        } else {
            return For::make(new_name, min, extent, op->for_type, op->partition_policy, op->device_api, body);
        }
    }

    Expr visit(const Variable *op) override {
        if (renaming.contains(op->name)) {
            string new_name = renaming.get(op->name);
            if (new_name != op->name) {
                return Variable::make(op->type, new_name);
            }
        }
        return op;
    }

public:
    UniquifyVariableNames(const Scope<string> *free_vars) {
        renaming.set_containing_scope(free_vars);
    }
};

class FindFreeVars : public IRVisitor {

    using IRVisitor::visit;

    Scope<> scope;

    void visit(const Variable *op) override {
        if (!scope.contains(op->name)) {
            free_vars.push(op->name, op->name);
        }
    }

    template<typename T>
    void visit_let(const T *op) {
        vector<ScopedBinding<>> frame;
        decltype(op->body) body;
        do {
            op->value.accept(this);
            frame.emplace_back(scope, op->name);
            body = op->body;
            op = body.template as<T>();
        } while (op);
        body.accept(this);
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

    void visit(const For *op) override {
        op->min.accept(this);
        op->extent.accept(this);
        {
            ScopedBinding<> bind(scope, op->name);
            op->body.accept(this);
        }
    }

public:
    Scope<string> free_vars;
};
}  // namespace

Stmt uniquify_variable_names(const Stmt &s) {
    FindFreeVars finder;
    s.accept(&finder);
    UniquifyVariableNames u(&finder.free_vars);
    return u.mutate(s);
}

void check(vector<pair<Var, Expr>> in,
           vector<pair<Var, Expr>> out) {
    Stmt in_stmt = Evaluate::make(0), out_stmt = Evaluate::make(0);
    for (auto it = in.rbegin(); it != in.rend(); it++) {
        in_stmt = LetStmt::make(it->first.name(), it->second, in_stmt);
    }
    for (auto it = out.rbegin(); it != out.rend(); it++) {
        out_stmt = LetStmt::make(it->first.name(), it->second, out_stmt);
    }

    Stmt s = uniquify_variable_names(in_stmt);

    internal_assert(equal(s, out_stmt))
        << "Failure in uniquify_variable_names\n"
        << "Input:\n"
        << in_stmt << "\n"
        << "Produced:\n"
        << s << "\n"
        << "Correct output:\n"
        << out_stmt << "\n";
}

void uniquify_variable_names_test() {
    Var x("x"), x_1("x_1"), x_2("x_2"), x_3{"x_3"};
    Var y("y"), y_1("y_1"), y_2("y_2"), y_3{"y_3"};

    // Stmts with all names already unique should be unchanged
    check({{x, 3},
           {y, x}},
          {{x, 3},
           {y, x}});

    // Shadowed definitions of Vars should be given unique names
    check({{x, 3},
           {y, x},
           {x, x + y},
           {y, x + y},
           {x, x + y},
           {y, x + y}},
          {{x, 3},
           {y, x},
           {x_1, x + y},
           {y_1, x_1 + y},
           {x_2, x_1 + y_1},
           {y_2, x_2 + y_1}});

    // Check a case with a free var after then end of the scope of a let of the same name
    check({{x, Let::make(y.name(), 3, y)},      // y is bound
           {x, y}},                             // This is not the same y. It's free and can't be renamed.
          {{x, Let::make(y_1.name(), 3, y_1)},  // We rename the bound one
           {x_1, y}});

    // An existing in-scope use of one of the names that would be
    // autogenerated should be skipped over
    check({{x_1, 8},
           {x, 3},
           {y, x},
           {x, x + y},
           {y, x + y},
           {x, x + y},
           {y, x + y}},
          {{x_1, 8},
           {x, 3},
           {y, x},
           {x_2, x + y},
           {y_1, x_2 + y},
           {x_3, x_2 + y_1},
           {y_2, x_3 + y_1}});

    // Check parallel bindings. The scope doesn't overlap so they can keep their name
    check({{x, Let::make(y.name(), 3, y)},
           {x, Let::make(y.name(), 4, y)}},
          {{x, Let::make(y.name(), 3, y)},
           {x_1, Let::make(y.name(), 4, y)}});

    std::cout << "uniquify_variable_names test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide
