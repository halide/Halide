#include <map>
#include <set>
#include <string>
#include <vector>

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "InferArguments.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

class InferArguments : public IRGraphVisitor {
public:
    vector<InferredArgument> &args;

    InferArguments(vector<InferredArgument> &a, const vector<Function> &o, const Stmt &body)
        : args(a), outputs(o) {
        args.clear();
        for (const Function &f : outputs) {
            visit_function(f);
        }
        if (body.defined()) {
            body.accept(this);
        }
    }

private:
    const vector<Function> outputs;
    set<string> visited_functions;

    struct ParamOrBuffer {
        Parameter param;
        Buffer<> buffer;
    };
    map<string, ParamOrBuffer> args_by_name;

    using IRGraphVisitor::visit;

    bool is_output_name(const string &name) const {
        for (const Function &output : outputs) {
            if (name == output.name() || starts_with(name, output.name() + ".")) {
                return true;
            }
        }
        return false;
    }

    static bool dupe_names_error(const string &name) {
        user_error << "All Params and embedded Buffers must have unique names, but the name '"
                   << name << "' was seen multiple times.\n";
        return false;  // not reached
    }

    bool already_have(const Parameter &p) {
        const string &name = p.name();

        // Ignore dependencies on the output buffers
        if (is_output_name(name)) {
            return true;
        }

        auto it = args_by_name.find(name);
        if (it == args_by_name.end()) {
            // If the Parameter is already bound to a Buffer, include it here.
            if (p.is_buffer() && p.buffer().defined()) {
                args_by_name[name] = {p, p.buffer()};
            } else {
                args_by_name[name] = {p, Buffer<>()};
            }
            return false;
        }

        ParamOrBuffer &pob = it->second;
        if (pob.param.defined()) {
            // If the name is already in the args, verify that it's the same
            // Parameter that we've already seen.
            if (p.same_as(pob.param)) {
                return true;
            } else {
                // Multiple different Parameters with the same name -> illegal
                return dupe_names_error(name);
            }
        } else if (pob.buffer.defined()) {
            // If the name is in the args, but only as a Buffer,
            // maybe it's the Buffer that the Parameter is bound to?
            if (p.is_buffer() && p.buffer().defined() && p.buffer().same_as(pob.buffer)) {
                // Update this entry to have both the Parameter and Buffer.
                pob.param = p;
                return true;
            } else {
                // A Parameter and Buffer with the same name (but aren't connected) -> illegal
                return dupe_names_error(name);
            }
        } else {
            internal_error << "There should be no empty ParamOrBuffers in the map.";
            return false;  // not reached
        }
    }

    bool already_have(const Buffer<> &b) {
        const string &name = b.name();

        // Ignore dependencies on the output buffers
        if (is_output_name(name)) {
            return true;
        }

        auto it = args_by_name.find(name);
        if (it == args_by_name.end()) {
            args_by_name[name] = {Parameter(), b};
            return false;
        }

        ParamOrBuffer &pob = it->second;
        if (pob.buffer.defined()) {
            // If the name is already in the args, verify that it's the same
            // Buffer that we've already seen.
            if (b.same_as(pob.buffer)) {
                return true;
            } else {
                // Multiple different Buffers with the same name -> illegal
                return dupe_names_error(name);
            }
        } else if (pob.param.defined()) {
            // If the name is in the args, but only as a Parameter,
            // maybe it's the Parameter that this Buffer is bound to?
            if (pob.param.is_buffer() && pob.param.buffer().same_as(b)) {
                // Update this entry to have both the Parameter and Buffer.
                pob.buffer = b;
                return true;
            } else {
                // A Parameter and Buffer with the same name (but aren't connected) -> illegal
                return dupe_names_error(name);
            }
        } else {
            internal_error << "There should be no empty ParamOrBuffers in the map.";
            return false;  // not reached
        }
    }

    void visit_exprs(const vector<Expr> &v) {
        for (const Expr &i : v) {
            visit_expr(i);
        }
    }

    void visit_expr(const Expr &e) {
        if (!e.defined()) {
            return;
        }
        e.accept(this);
    }

    void visit_function(const Function &func) {
        if (visited_functions.count(func.name())) {
            return;
        }
        visited_functions.insert(func.name());

        func.accept(this);

        // Function::accept hits all the Expr children of the
        // Function, but misses the buffers and images that might be
        // extern arguments.
        if (func.has_extern_definition()) {
            for (const ExternFuncArgument &extern_arg : func.extern_arguments()) {
                if (extern_arg.is_func()) {
                    visit_function(Function(extern_arg.func));
                } else if (extern_arg.is_buffer()) {
                    include_buffer(extern_arg.buffer);
                } else if (extern_arg.is_image_param()) {
                    include_parameter(extern_arg.image_param);
                }
            }
        }

        // It also misses wrappers
        for (const auto &p : func.wrappers()) {
            Function(p.second).accept(this);
        }
    }

    void include_parameter(const Parameter &p) {
        if (!p.defined() ||
            already_have(p)) {
            return;
        }

        ArgumentEstimates argument_estimates = p.get_argument_estimates();
        if (!p.is_buffer()) {
            // We don't want to crater here if a scalar param isn't set;
            // instead, default to a zero of the right type, like we used to.
            argument_estimates.scalar_def = p.has_scalar_value() ? p.scalar_expr() : make_zero(p.type());
            argument_estimates.scalar_min = p.min_value();
            argument_estimates.scalar_max = p.max_value();
            argument_estimates.scalar_estimate = p.estimate();
        }

        InferredArgument a = {
            Argument(p.name(),
                     p.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                     p.type(), p.dimensions(), argument_estimates),
            p,
            Buffer<>()};
        args.push_back(a);

        // Visit child expressions
        visit_expr(argument_estimates.scalar_def);
        visit_expr(argument_estimates.scalar_min);
        visit_expr(argument_estimates.scalar_max);
        visit_expr(argument_estimates.scalar_estimate);
        for (const auto &be : argument_estimates.buffer_estimates) {
            visit_expr(be.min);
            visit_expr(be.extent);
        }

        if (p.is_buffer()) {
            for (int i = 0; i < p.dimensions(); i++) {
                visit_expr(p.min_constraint(i));
                visit_expr(p.extent_constraint(i));
                visit_expr(p.stride_constraint(i));
            }
        }
    }

    void include_buffer(const Buffer<> &b) {
        if (!b.defined() ||
            already_have(b)) {
            return;
        }

        InferredArgument a = {
            Argument(b.name(), Argument::InputBuffer, b.type(), b.dimensions(), ArgumentEstimates{}),
            Parameter(),
            b};
        args.push_back(a);
    }

    void visit(const Load *op) override {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Variable *op) override {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->func.defined()) {
            Function fn(op->func);
            visit_function(fn);
        }
        include_buffer(op->image);
        include_parameter(op->param);
    }
};

}  // namespace

vector<InferredArgument> infer_arguments(const Stmt &body, const vector<Function> &outputs) {
    vector<InferredArgument> inferred_args;
    // Infer an arguments vector by walking the IR
    InferArguments infer_args(inferred_args,
                              outputs,
                              body);

    // Sort the Arguments with all buffers first (alphabetical by name),
    // followed by all non-buffers (alphabetical by name).
    std::sort(inferred_args.begin(), inferred_args.end());

    return inferred_args;
}

}  // namespace Internal
}  // namespace Halide
