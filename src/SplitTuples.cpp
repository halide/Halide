#include "SplitTuples.h"
#include "Bounds.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

// Collect all value indices of internal halide calls.
class FindCallValueIndices : public IRVisitor {
public:
    const string func;
    map<string, set<int>> func_value_indices;

    using IRVisitor::visit;

    void visit(const Call *call) override {
        IRVisitor::visit(call);
        if ((call->call_type == Call::Halide) && call->func.defined()) {
            func_value_indices[call->name].insert(call->value_index);
        }
    }
};

// Visitor and helper function to test if a piece of IR uses an extern image.
class UsesExternImage : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *c) override {
        if (c->call_type == Call::Image) {
            result = true;
        } else {
            IRVisitor::visit(c);
        }
    }
public:
    UsesExternImage() : result(false) {}
    bool result;
};

inline bool uses_extern_image(Stmt s) {
    UsesExternImage uses;
    s.accept(&uses);
    return uses.result;
}

class SplitTuples : public IRMutator2 {
    using IRMutator2::visit;

    map<string, set<int>> func_value_indices;

    Stmt visit(const Realize *op) override {
        ScopedBinding<int> bind(realizations, op->name, 0);
        if (op->types.size() > 1) {
            // Make a nested set of realize nodes for each tuple element
            Stmt body = mutate(op->body);
            for (int i = (int)op->types.size() - 1; i >= 0; i--) {
                body = Realize::make(op->name + "." + std::to_string(i), {op->types[i]}, op->memory_type, op->bounds, op->condition, body);
            }
            return body;
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        map<string, set<int>> old_func_value_indices = func_value_indices;

        FindCallValueIndices find;
        op->body.accept(&find);

        func_value_indices = find.func_value_indices;
        Stmt stmt = IRMutator2::visit(op);
        func_value_indices = old_func_value_indices;
        return stmt;
    }

    Stmt visit(const Prefetch *op) override {
        if (!op->prefetch.param.defined() && (op->types.size() > 1)) {
            Stmt body = mutate(op->body);
            // Split the prefetch from a multi-dimensional halide tuple to
            // prefetches of each tuple element. Keep only prefetches of
            // elements that are actually used in the loop body.
            const auto &indices = func_value_indices.find(op->name);
            internal_assert(indices != func_value_indices.end());

            for (const auto &idx : indices->second) {
                internal_assert(idx < (int)op->types.size());
                body = Prefetch::make(op->name + "." + std::to_string(idx), {op->types[(idx)]}, op->bounds, op->prefetch, op->condition, body);
            }
            return body;
        } else {
            return IRMutator2::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::Halide) {
            auto it = env.find(op->name);
            internal_assert(it != env.end());
            Function f = it->second;
            string name = op->name;
            if (f.outputs() > 1) {
                name += "." + std::to_string(op->value_index);
            }
            vector<Expr> args;
            for (Expr e : op->args) {
                args.push_back(mutate(e));
            }
            // It's safe to hook up the pointer to the function
            // unconditionally. This expr never gets held by a
            // Function, so there can't be a cycle. We do this even
            // for scalar provides.
            return Call::make(op->type, name, args, op->call_type, f.get_contents());
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const Provide *op) override {
        if (op->values.size() == 1) {
            return IRMutator2::visit(op);
        }

        // Detect if the provide needs to be lowered atomically. By
        // this we mean can we compute and store the values one at a
        // time (not atomic), or must we compute them all, and then
        // store them all (atomic).
        bool atomic = false;
        if (!realizations.contains(op->name) &&
            uses_extern_image(op)) {
            // If the provide is an output (it's not inside a
            // realization), and it uses an input, then the input
            // might alias with the output. We'd better just do it
            // atomically.
            atomic = true;
        } else {
            // If the boxes provided and required might overlap,
            // the provide must be done atomically.
            Box provided = box_provided(op, op->name);
            Box required = box_required(op, op->name);
            atomic = boxes_overlap(provided, required);
        }

        // Mutate the args
        vector<Expr> args;
        for (Expr e : op->args) {
            args.push_back(mutate(e));
        }

        // Get the Function
        auto it = env.find(op->name);
        internal_assert(it != env.end());
        Function f = it->second;

        // Build a list of scalar provide statements, and a list of
        // lets to wrap them.
        vector<Stmt> provides;
        vector<pair<string, Expr>> lets;

        for (size_t i = 0; i < op->values.size(); i++) {
            string name = op->name + "." + std::to_string(i);
            string var_name = name + ".value";
            Expr val = mutate(op->values[i]);
            if (!is_undef(val) && atomic) {
                lets.push_back({ var_name, val });
                val = Variable::make(val.type(), var_name);
            }
            provides.push_back(Provide::make(name, {val}, args));
        }

        Stmt result = Block::make(provides);

        while (!lets.empty()) {
            auto p = lets.back();
            lets.pop_back();
            result = LetStmt::make(p.first, p.second, result);
        }

        return result;
    }

    const map<string, Function> &env;
    Scope<int> realizations;

public:

    SplitTuples(const map<string, Function> &e) : env(e) {}
};

}  // namespace

Stmt split_tuples(Stmt s, const map<string, Function> &env) {
    return SplitTuples(env).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
