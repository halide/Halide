#include <set>
#include <string>
#include <vector>

#include "InferArguments.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;

namespace {

class InferArguments : public IRGraphVisitor {
public:
    vector<InferredArgument> &args;

    InferArguments(vector<InferredArgument> &a, const vector<Function> &o, Stmt body)
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
    vector<Function> outputs;
    set<string> visited_functions;

    using IRGraphVisitor::visit;

    bool already_have(const string &name) {
        // Ignore dependencies on the output buffers
        for (const Function &output : outputs) {
            if (name == output.name() || starts_with(name, output.name() + ".")) {
                return true;
            }
        }
        for (const InferredArgument &arg : args) {
            if (arg.arg.name == name) {
                return true;
            }
        }
        return false;
    }

    void visit_exprs(const vector<Expr>& v) {
        for (Expr i : v) {
            visit_expr(i);
        }
    }

    void visit_expr(Expr e) {
        if (!e.defined()) return;
        e.accept(this);
    }

    void visit_function(const Function& func) {
        if (visited_functions.count(func.name())) return;
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
    }

    void include_parameter(Parameter p) {
        if (!p.defined()) return;
        if (p.is_bound_before_lowering()) return;
        if (already_have(p.name())) return;

        Expr def, min, max;
        if (!p.is_buffer()) {
            def = p.scalar_expr();
            min = p.min_value();
            max = p.max_value();
        }

        InferredArgument a = {
            Argument(p.name(),
                     p.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                     p.type(), p.dimensions(), def, min, max),
            p,
            Buffer<>()};
        args.push_back(a);

        // Visit child expressions
        if (!p.is_buffer()) {
            visit_expr(def);
            visit_expr(min);
            visit_expr(max);
        } else {
            for (int i = 0; i < p.dimensions(); i++) {
                visit_expr(p.min_constraint(i));
                visit_expr(p.extent_constraint(i));
                visit_expr(p.stride_constraint(i));
            }
        }
    }

    void include_buffer(Buffer<> b) {
        if (!b.defined()) return;
        if (already_have(b.name())) return;

        InferredArgument a = {
            Argument(b.name(), Argument::InputBuffer, b.type(), b.dimensions()),
            Parameter(),
            b};
        args.push_back(a);
    }

    void visit(const Load *op) {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Variable *op) {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->func.defined()) {
            Function fn(op->func);
            visit_function(fn);
        }
        include_buffer(op->image);
        include_parameter(op->param);
    }
};

}

vector<InferredArgument> infer_arguments(Stmt body, const vector<Function> &outputs) {
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

}
}
