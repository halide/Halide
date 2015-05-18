#include "InjectImageIntrinsics.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CodeGen_GPU_Dev.h"
#include "Substitute.h"
#include "FuseGPUThreadLoops.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class InjectImageIntrinsics : public IRMutator {
public:
    InjectImageIntrinsics() : inside_kernel_loop(false) {}
    Scope<int> scope;
    bool inside_kernel_loop;

private:
    using IRMutator::visit;

    void visit(const Provide *provide) {
        if (!inside_kernel_loop) {
            IRMutator::visit(provide);
            return;
        }

        internal_assert(provide->values.size() == 1)
            << "Image currently only supports single-valued stores.\n";
        user_assert(provide->args.size() == 3)
            << "Image stores require three coordinates.\n";

        // Create image_store("name", name.buffer, x, y, c, value)
        // intrinsic.
        Expr value_arg = mutate(provide->values[0]);
        vector<Expr> args = {
            provide->name,
            Variable::make(Handle(), provide->name + ".buffer"),
            provide->args[0],
            provide->args[1],
            provide->args[2],
            value_arg};

        stmt = Evaluate::make(Call::make(value_arg.type(),
                                         Call::image_store,
                                         args,
                                         Call::Intrinsic));
    }

    void visit(const Call *call) {
        if (!inside_kernel_loop || call->call_type == Call::Intrinsic ||
            call->call_type == Call::Extern) {
            IRMutator::visit(call);
            return;
        }

        string name = call->name;
        if (call->call_type == Call::Halide && call->func.outputs() > 1) {
            name = name + '.' + int_to_string(call->value_index);
        }

        vector<Expr> padded_call_args = call->args;
        // Check to see if we are reading from a one or two dimension function
        // and pad to three dimensions.
        while (padded_call_args.size() < 3) {
            padded_call_args.push_back(IntImm::make(0));
        }

        // Create image_load("name", name.buffer, x, x_extent, y, y_extent, ...).
        // Extents can be used by successive passes. OpenGL, for example, uses them
        // for coordinates normalization.
        vector<Expr> args(2);
        args[0] = call->name;
        args[1] = Variable::make(Handle(), call->name + ".buffer");
        for (size_t i = 0; i < padded_call_args.size(); i++) {

            // If this is an ordinary dimension, insert a variable that will be
            // subsequently defined by StorageFlattening to with the min and
            // extent. Otherwise, add a default value for the padded dimension.
            // If 'i' is greater or equal to the number of args in the original
            // node, it must be a padded dimension we added above.
            if (i < call->args.size()) {
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
                args.push_back(mutate(padded_call_args[i]) - min);
                args.push_back(Variable::make(Int(32), extent_name));
            } else {
                args.push_back(IntImm::make(0));
                args.push_back(IntImm::make(1));
            }
        }

        Type load_type = call->type;
        // load_type.width = 4;

        Expr load_call = Call::make(load_type,
                          Call::image_load,
                          args,
                          Call::Intrinsic,
                          Function(),
                          0,
                          call->image,
                          call->param);
        expr = load_call;
        // expr = Call::make(call->type, Call::shuffle_vector,
        //                   vec(load_call, args[4]), Call::Intrinsic);
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
        if (loop->for_type == ForType::Parallel &&
            (loop->device_api == DeviceAPI::GLSL ||
                loop->device_api == DeviceAPI::Renderscript)) {
            inside_kernel_loop = true;
        }
        IRMutator::visit(loop);
        inside_kernel_loop = old_kernel_loop;
    }
};

Stmt inject_image_intrinsics(Stmt s) {
    debug(4)
        << "InjectImageIntrinsics: inject_image_intrinsics stmt: "
        << s << "\n";
    s = zero_gpu_loop_mins(s);
    InjectImageIntrinsics gl;
    return gl.mutate(s);
}
}
}
