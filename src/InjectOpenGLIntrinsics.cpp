#include "InjectOpenGLIntrinsics.h"
#include "CodeGen_GPU_Dev.h"
#include "FuseGPUThreadLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

/** Normalizes image loads/stores and produces glsl_texture_load/stores. */
class InjectOpenGLIntrinsics : public IRMutator {
public:
    InjectOpenGLIntrinsics()
        : inside_kernel_loop(false) {
    }
    Scope<int> scope;
    bool inside_kernel_loop;
private:
    using IRMutator::visit;

    Expr visit(const Call *call) override {
        if (call->is_intrinsic(Call::image_load)) {
            vector<Expr> call_args = call->args;
            //
            // Create
            //  glsl_texture_load("name",
            //                    name.buffer,
            //                    (x - x_min + 0.5)/x_extent,
            //                    (y - y_min + 0.5)/y_extent,
            //                    c)
            // from
            //  image_load("name",
            //                   name.buffer,
            //                   x - x_min, x_extent,
            //                   y - y_min, y_extent,
            //                   c - c_min, c_extent
            //                   )
            //
            vector<Expr> args(5);
            args[0] = call_args[0];    // "name"
            args[1] = call_args[1];    // name.buffer

            // Normalize first two coordinates.
            for (size_t i = 0; i < 2; i++) {
                int to_index = 2 + i;
                int from_index = 2 + i * 2;
                args[to_index] =
                  (Cast::make(Float(32), mutate(call_args[from_index])) + 0.5f) /
                  mutate(call_args[from_index + 1]);
            }

            // Confirm that user explicitly specified constant value for min
            // value of c dimension for ImageParams accessed by GLSL-based filters.
            if (call->param.defined()) {
                bool const_min_constraint =
                    call->param.min_constraint(2).defined() &&
                    is_const(call->param.min_constraint(2));
                user_assert(const_min_constraint)
                    << "GLSL: Requires minimum for c-dimension set to constant "
                    << "for ImageParam '" << args[0] << "'. "
                    << "Call set_min(2, min) or set_bounds(2, min, extent) to set.\n";
            }

            Expr c_coordinate = mutate(call_args[2 + 2 * 2]);
            args[4] = c_coordinate;

            return Call::make(call->type, Call::glsl_texture_load,
                              vector<Expr>(&args[0], &args[5]),
                              Call::Intrinsic, FunctionPtr(), 0,
                              call->image, call->param);
        } else if (call->is_intrinsic(Call::image_store)) {
            user_assert(call->args.size() == 6)
                << "GLSL stores require three coordinates.\n";

            // Create
            //    gl_texture_store(name, name.buffer, x, y, c, value)
            // out of
            //    image_store(name, name.buffer, x, y, c, value)
            vector<Expr> args(call->args);
            args[5] = mutate(call->args[5]); // mutate value
            return Call::make(call->type, Call::glsl_texture_store,
                              args, Call::Intrinsic);
        } else {
            return IRMutator::visit(call);
        }
    }
};

Stmt inject_opengl_intrinsics(Stmt s) {
    InjectOpenGLIntrinsics gl;
    return gl.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
