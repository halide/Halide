#include "InjectCoordinatesIntrinsics.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CodeGen_GPU_Dev.h"
#include "Substitute.h"
#include "FuseGPUThreadLoops.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class InjectCoordinatesIntrinsics : public IRMutator {
public:
    InjectCoordinatesIntrinsics() : inside_kernel_loop(false) {}
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
            << "Coordinate currently only supports single-valued stores.\n";
        user_assert(provide->args.size() == 3)
            << "Coordinate stores require three coordinates.\n";

        // Create coordinate_store("name", name.buffer, x, y, c, value)
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
                                         Call::coordinates_store,
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

        vector<Expr> call_args = call->args;
        // Check to see if we are reading from a one or two dimension function
        // and pad to three dimensions.
        while (call_args.size() < 3) {
            call_args.push_back(IntImm::make(0));
        }

        // Create coordinates_load("name", "name[.n]", name.buffer, x, y, c)
        // intrinsic call.
        // We need to pass "name[.n]" because if we need to add normalization
        // then we will this name as a prefix for "name[.n].extent" variable.
        vector<Expr> args(6);
        args[0] = call->name;
        args[1] = name;
        args[2] = Variable::make(Handle(), call->name + ".buffer");
        for (size_t i = 0; i < call_args.size(); i++) {
            string d = int_to_string(i);
            string min_name = name + ".min." + d;
            string min_name_constrained = min_name + ".constrained";
            if (scope.contains(min_name_constrained)) {
                min_name = min_name_constrained;
            }

            Expr min = Variable::make(Int(32), min_name);

            // Remind users to explicitly specify the 'min' values of
            // ImageParams accessed by coordinate-based filters.
            if (i == 2 && call->param.defined()) {
                bool const_min_constraint =
                    call->param.min_constraint(i).defined() &&
                    is_const(call->param.min_constraint(i));
                if (!const_min_constraint) {
                    user_warning
                        << "Coordinates: Assuming min[2]==0 for ImageParam '"
                        << name << "'. "
                        << "Call set_min(2, min) or set_bounds(2, "
                           "min, extent) to override.\n";
                    min = Expr(0);
                }
            }

            // Inject intrinsics into the call argument
            Expr arg = mutate(call_args[i]);
            debug(4) << "Subtracting min from arg. arg:" << arg
                     << " min:" << min << "\n";

            args[i + 3] = arg - min;
        }

        expr = Call::make(call->type,
                          Call::coordinates_load,
                          args,
                          Call::Intrinsic,
                          Function(),
                          0,
                          call->image,
                          call->param);
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
            loop->device_api == DeviceAPI::GLSL) {
            inside_kernel_loop = true;
        }
        IRMutator::visit(loop);
        inside_kernel_loop = old_kernel_loop;
    }
};

Stmt inject_coordinates_intrinsics(Stmt s) {
    debug(4)
        << "InjectCoordinatesIntrinsics: inject_coordinates_intrinsics stmt: "
        << s << "\n";
    s = zero_gpu_loop_mins(s);
    InjectCoordinatesIntrinsics gl;
    return gl.mutate(s);
}
}
}
