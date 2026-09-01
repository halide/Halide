#include "UniquifyVariableNames.h"
#include "FreeVariables.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {
class UniquifyVariableNames : public IRMutator {
protected:
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

        for (const auto &frame : reverse_view(frames)) {
            renaming.pop(frame.op->name);
            if (frame.new_name == frame.op->name &&
                result.same_as(frame.op->body) &&
                frame.op->value.same_as(frame.value)) {
                result = frame.op;
            } else {
                result = LetOrLetStmt::make(frame.new_name, frame.value, result);
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
        Expr max = mutate(op->max);
        string new_name = make_new_name(op->name);
        Stmt body = mutate(op->body);
        renaming.pop(op->name);

        if (new_name == op->name &&
            body.same_as(op->body) &&
            min.same_as(op->min) &&
            max.same_as(op->max)) {
            return op;
        } else {
            return For::make(new_name, min, max, op->for_type, op->partition_policy, op->device_api, body);
        }
    }

    Expr visit(const Variable *op) override {
        if (const string *new_name = renaming.find(op->name)) {
            if (*new_name != op->name) {
                return Variable::make(op->type, *new_name);
            }
        }
        return op;
    }

public:
    UniquifyVariableNames(const Scope<string> *free_vars) {
        renaming.set_containing_scope(free_vars);
    }
};

}  // namespace

Stmt uniquify_variable_names(const Stmt &s) {
    std::map<string, Type> free_vars = find_free_vars(s);
    Scope<string> free_var_names;
    for (const auto &p : free_vars) {
        free_var_names.push(p.first, p.first);
    }
    return UniquifyVariableNames(&free_var_names)(s);
}

}  // namespace Internal
}  // namespace Halide
