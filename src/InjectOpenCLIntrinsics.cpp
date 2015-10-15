#include "InjectOpenCLIntrinsics.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CodeGen_GPU_Dev.h"
#include "Substitute.h"
#include "FuseGPUThreadLoops.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class InjectOpenCLIntrinsics : public IRMutator {
public:
    InjectOpenCLIntrinsics() : inside_kernel_loop(false) {}
    Scope<int> realizations;
    bool inside_kernel_loop;

private:
    using IRMutator::visit;

    void visit(const Provide *provide) {
        if (!inside_kernel_loop
            || provide->args.size() < 2
            || provide->args.size() > 3
            || realizations.contains(provide->name)) {
            IRMutator::visit(provide);
            return;
        }

        if (provide->values.size() > 1) {
            // Flatten this provide into a sequence of write_images
            Stmt result;
            for (size_t i = 0; i < provide->values.size(); i++) {
                Expr value = mutate(provide->values[i]);
                Type t = value.type();
                t.bits = t.bytes() * 8;
                if (t.bits != value.type().bits) {
                    value = Cast::make(t, value);
                }

                string name = provide->name + "." + std::to_string(i);
                vector<Expr> args;
                args.push_back(name);
                for (const Expr &arg : provide->args) {
                    args.push_back(arg);
                }
                args.push_back(value);
                Stmt write_image = Evaluate::make(Call::make(value.type(),
                                                           Call::write_image,
                                                           args,
                                                           Call::Intrinsic));
                if (result.defined()) {
                    result = Block::make(result, write_image);
                } else {
                    result = write_image;
                }
            }
            stmt = result;
        } else {
            // Create write_image("name", x, y, value) intrinsic.
            Expr value_arg = mutate(provide->values[0]);
            vector<Expr> args;
            args.push_back(provide->name);
            for (const Expr arg : provide->args) {
                args.push_back(arg);
            }
            args.push_back(value_arg);
            stmt = Evaluate::make(Call::make(value_arg.type(),
                                             Call::write_image,
                                             args,
                                             Call::Intrinsic));
        }
    }

    void visit(const Call *call) {
        if (!inside_kernel_loop
            || call->args.size() < 2
            || call->args.size() > 3
            || (call->call_type != Call::Image && call->call_type != Call::Halide)
            || realizations.contains(call->name)) {
            IRMutator::visit(call);
            return;
        }

        string name = call->name;
        if (call->call_type == Call::Halide && call->func.outputs() > 1) {
            name += "." + std::to_string(call->value_index);
        }

        vector<Expr> args;
        args.push_back(name);
        for (const Expr &arg : call->args) {
            args.push_back(arg);
        }
        expr = Call::make(call->type,
                          Call::read_image,
                          args,
                          Call::Intrinsic);

    }

    void visit(const Realize *realize) {
        if (inside_kernel_loop) {
            realizations.push(realize->name, 1);
            IRMutator::visit(realize);
            realizations.pop(realize->name);
        } else {
            IRMutator::visit(realize);
        }
    }

    void visit(const For *loop) {
        bool old_kernel_loop = inside_kernel_loop;
        if (loop->for_type == ForType::Parallel &&
            (loop->device_api == DeviceAPI::Default_GPU
             || loop->device_api == DeviceAPI::OpenCL)) {
            inside_kernel_loop = true;
        }
        IRMutator::visit(loop);
        inside_kernel_loop = old_kernel_loop;
    }
};

Stmt inject_opencl_intrinsics(Stmt s) {
    debug(4)
        << "InjectOpenCLIntrinsics: inject_opencl_intrinsics stmt: "
        << s << "\n";
    s = zero_gpu_loop_mins(s);
    InjectOpenCLIntrinsics cl;
    return cl.mutate(s);
}
}
}
