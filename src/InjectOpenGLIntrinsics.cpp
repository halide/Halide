#include "InjectOpenGLIntrinsics.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class InjectOpenGLIntrinsics : public IRMutator {
public:
    InjectOpenGLIntrinsics(Scope<int> &need_buffer_t)
        : need_buffer_t(need_buffer_t), inside_kernel_loop(false) {
    }
    Scope<int> scope;
    Scope<int> &need_buffer_t;
    bool inside_kernel_loop;
private:
    using IRMutator::visit;

    static float max_value(const Type &type) {
        if (type == UInt(8)) {
            return 255.0f;
        } else if (type == UInt(16)) {
            return 65535.0f;
        } else {
            internal_error << "Cannot determine max_value of type '" << type << "'\n";
        }
        return 1.0f;
    }

    void visit(const Provide *provide) {
        if (!inside_kernel_loop) {
            IRMutator::visit(provide);
            return;
        }

        internal_assert(provide->values.size() == 1) << "GLSL currently only supports scalar stores.\n";
        user_assert(provide->args.size() == 3) << "GLSL stores requires three coordinates.\n";

        // Record that this buffer is accessed from a GPU kernel
        need_buffer_t.push(provide->name, 0);

        // Create glsl_texture_store(name, x, y, c, value, name.buffer)
        // intrinsic.  Since the intrinsic only stores Float(32) values, the
        // original value type is encoded in first argument.
        vector<Expr> args(6);
        Expr value = mutate(provide->values[0]);
        args[0] = Variable::make(value.type(), provide->name);
        for (size_t i = 0; i < provide->args.size(); i++) {
            args[i + 1] = provide->args[i];
        }
        // TODO: Find a better way to prevent roundoff error.
        const float epsilon = 1/65535.f;
        args[4] = Div::make(Cast::make(Float(32), value) + epsilon,
                            max_value(value.type()));
        args[5] = Variable::make(Handle(), provide->name + ".buffer");
        stmt = Evaluate::make(
            Call::make(Float(32), "glsl_texture_store", args, Call::Intrinsic));
    }

    void visit(const Call *call) {
        if (!inside_kernel_loop || call->call_type == Call::Intrinsic) {
            IRMutator::visit(call);
            return;
        }

        string name = call->name;
        if (call->call_type == Call::Halide && call->func.outputs() > 1) {
            name = name + '.' + int_to_string(call->value_index);
        }

        user_assert(call->args.size() == 3) << "GLSL loads requires three coordinates.\n";

        // Record that this buffer is accessed from a GPU kernel
        need_buffer_t.push(call->name, 0);

        // Create glsl_texture_load(name, x, y, c, name.buffer) intrinsic.
        // Since the intrinsic always returns Float(32), the original type is
        // encoded in first argument.
        vector<Expr> args(5);
        args[0] = Variable::make(call->type, call->name);
        for (size_t i = 0; i < call->args.size(); i++) {
            string d = int_to_string(i);
            string min_name = name + ".min." + d;
            string min_name_constrained = min_name + ".constrained";
            if (scope.contains(min_name_constrained)) {
                min_name = min_name_constrained;
            }
            string extent_name = name + ".extent." + d;
            string extent_name_constrained = extent_name + ".constrained";
            if (scope.contains(extent_name_constrained)) {
                extent_name = extent_name_constrained;
            }

            Expr min = Variable::make(Int(32), min_name);
            Expr extent = Variable::make(Int(32), extent_name);

            // Normalize the two spatial coordinates x,y
            args[i + 1] = (i < 2)
                ? (Cast::make(Float(32), call->args[i] - min) + 0.5f) / extent
                : call->args[i] - min;
        }
        args[4] = Variable::make(Handle(), call->name + ".buffer");

        Expr load = Call::make(Float(32), "glsl_texture_load",
                               args, Call::Intrinsic,
                               Function(), 0, call->image, call->param);
        expr = Cast::make(call->type,
                          Mul::make(load, max_value(call->type)));
    }

    void visit(const LetStmt *let) {
        // Discover constrained versions of things.
        bool constrained_version_exists = ends_with(let->name, ".constrained");
        if (constrained_version_exists) {
            scope.push(let->name, 0);
        }

        IRMutator::visit(let);

        if (constrained_version_exists) {
            scope.pop(let->name);
        }
    }

    void visit(const For *loop) {
        bool old_kernel_loop = inside_kernel_loop;
        if (loop->for_type == For::Parallel &&
            (ends_with(loop->name, ".blockidx") || ends_with(loop->name, ".blockidy"))) {
            inside_kernel_loop = true;
        }
        IRMutator::visit(loop);
        inside_kernel_loop = old_kernel_loop;
    }
};

Stmt inject_opengl_intrinsics(Stmt s, Scope<int> &needs_buffer_t) {
    return InjectOpenGLIntrinsics(needs_buffer_t).mutate(s);
}

}
}
