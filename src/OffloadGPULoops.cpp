#include <memory>

#include "Closure.h"
#include "CodeGen_D3D12Compute_Dev.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Metal_Dev.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_OpenGLCompute_Dev.h"
#include "CodeGen_PTX_Dev.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "InjectHostDevBufferCopies.h"
#include "OffloadGPULoops.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {

// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the
// amount of shared memory to allocate.
class ExtractBounds : public IRVisitor {
public:
    Expr num_threads[4];
    Expr num_blocks[4];
    Expr shared_mem_size;

    ExtractBounds()
        : shared_mem_size(0) {
        for (int i = 0; i < 4; i++) {
            num_threads[i] = num_blocks[i] = 1;
        }
    }

private:
    bool found_shared = false;

    using IRVisitor::visit;

    void visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            internal_assert(is_const_zero(op->min));
        }

        if (ends_with(op->name, ".__thread_id_x")) {
            num_threads[0] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_y")) {
            num_threads[1] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_z")) {
            num_threads[2] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_w")) {
            num_threads[3] = op->extent;
        } else if (ends_with(op->name, ".__block_id_x")) {
            num_blocks[0] = op->extent;
        } else if (ends_with(op->name, ".__block_id_y")) {
            num_blocks[1] = op->extent;
        } else if (ends_with(op->name, ".__block_id_z")) {
            num_blocks[2] = op->extent;
        } else if (ends_with(op->name, ".__block_id_w")) {
            num_blocks[3] = op->extent;
        }

        op->body.accept(this);
    }

    void visit(const LetStmt *op) override {
        if (expr_uses_var(shared_mem_size, op->name)) {
            shared_mem_size = Let::make(op->name, op->value, shared_mem_size);
        }
        op->body.accept(this);
    }

    void visit(const Allocate *allocate) override {
        user_assert(!allocate->new_expr.defined()) << "Allocate node inside GPU kernel has custom new expression.\n"
                                                   << "(Memoization is not supported inside GPU kernels at present.)\n";

        if (allocate->memory_type == MemoryType::GPUShared) {
            internal_assert(allocate->extents.size() == 1);
            shared_mem_size += allocate->extents[0] * allocate->type.bytes();
            found_shared = true;
        }
        allocate->body.accept(this);
    }
};

Expr make_type_arg(const Type &t) {
    vector<Expr> args = {
        cast<uint8_t>(t.code()),
        cast<uint8_t>(t.bits()),
        cast<uint16_t>(t.lanes()),
    };
    return Call::make(type_of<void *>(), Call::make_struct, args, Call::Intrinsic);
}

class InjectGpuOffload : public IRMutator {
    /** Child code generator for device kernels. */
    map<DeviceAPI, unique_ptr<CodeGen_GPU_Dev>> cgdev;

    map<string, Expr> state_bufs;

    const Target &target;

    Expr state_var(const string &name, Type type, bool create) {
        Expr ptr = state_var_ptr(name, type, create);
        if (!ptr.defined()) {
            return Expr();
        }
        return Let::make(name, ptr,
                         Load::make(type_of<void *>(), name, 0,
                                    Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
    }

    Expr state_var_ptr(const string &name, Type type, bool create) {
        Expr &buf = state_bufs[name];
        if (!buf.defined() && create) {
            auto storage = Buffer<void *>::make_scalar(name + "_buf");
            storage() = nullptr;
            buf = Variable::make(type_of<halide_buffer_t *>(), storage.name() + ".buffer", storage);
        }
        if (buf.defined()) {
            return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
        } else {
            return Expr();
        }
    }

    Expr module_state(const string &api_name, bool create = true) {
        return state_var(api_name, type_of<void *>(), create);
    }

    Expr module_state_ptr(const string &api_name, bool create = true) {
        return state_var_ptr(api_name, type_of<void *>(), create);
    }

    // Create a Buffer containing the given vector, and return an
    // expression for a pointer to the first element.
    Expr make_buffer_ptr(const vector<char> &data, const string &name) {
        Buffer<uint8_t> code((int)data.size(), name);
        memcpy(code.data(), data.data(), (int)data.size());
        Expr buf = Variable::make(type_of<halide_buffer_t *>(), name + ".buffer", code);
        return Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
    }

    using IRMutator::visit;

    Stmt visit(const For *loop) override {
        if (!CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
            return IRMutator::visit(loop);
        }

        // We're in the loop over outermost block dimension
        debug(2) << "Kernel launch: " << loop->name << "\n";

        internal_assert(loop->device_api != DeviceAPI::Default_GPU)
            << "A concrete device API should have been selected before codegen.";

        ExtractBounds bounds;
        loop->accept(&bounds);
        debug(2) << "Kernel bounds: ("
                 << bounds.num_threads[0] << ", "
                 << bounds.num_threads[1] << ", "
                 << bounds.num_threads[2] << ", "
                 << bounds.num_threads[3] << ") threads, ("
                 << bounds.num_blocks[0] << ", "
                 << bounds.num_blocks[1] << ", "
                 << bounds.num_blocks[2] << ", "
                 << bounds.num_blocks[3] << ") blocks\n";

        // compute a closure over the state passed into the kernel
        HostClosure c(loop->body, loop->name);

        // Determine the arguments that must be passed into the halide function
        vector<DeviceArgument> closure_args = c.arguments();

        // Sort the args by the size of the underlying type. This is
        // helpful for avoiding struct-packing ambiguities in metal,
        // which passes the scalar args as a struct.
        sort(closure_args.begin(), closure_args.end(),
             [](const DeviceArgument &a, const DeviceArgument &b) {
                 if (a.is_buffer == b.is_buffer) {
                     return a.type.bits() > b.type.bits();
                 } else {
                     // Ensure that buffer arguments come first:
                     // for many OpenGL/Compute systems, the
                     // legal indices for buffer args are much
                     // more restrictive than for scalar args,
                     // and scalar args can be 'grown' by
                     // LICM. Putting buffers first makes it much
                     // more likely we won't fail on some
                     // hardware.
                     return a.is_buffer > b.is_buffer;
                 }
             });

        // compile the kernel
        string kernel_name = c_print_name(unique_name("kernel_" + loop->name));

        CodeGen_GPU_Dev *gpu_codegen = cgdev[loop->device_api].get();
        user_assert(gpu_codegen != nullptr)
            << "Loop is scheduled on device " << loop->device_api
            << " which does not appear in target " << target.to_string() << "\n";
        gpu_codegen->add_kernel(loop, kernel_name, closure_args);

        // get the actual name of the generated kernel for this loop
        kernel_name = gpu_codegen->get_current_kernel_name();
        debug(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";

        bool runtime_run_takes_types = gpu_codegen->kernel_run_takes_types();
        Type target_size_t_type = target.bits == 32 ? Int(32) : Int(64);

        vector<Expr> args, arg_types_or_sizes, arg_is_buffer;
        for (const DeviceArgument &i : closure_args) {
            Expr val;
            if (i.is_buffer) {
                val = Variable::make(Handle(), i.name + ".buffer");
            } else {
                val = Variable::make(i.type, i.name);
                val = Call::make(type_of<void *>(), Call::make_struct, {val}, Call::Intrinsic);
            }
            args.push_back(val);

            if (runtime_run_takes_types) {
                arg_types_or_sizes.push_back(make_type_arg(i.type.with_lanes(1)));
            } else {
                arg_types_or_sizes.push_back(cast(target_size_t_type, i.is_buffer ? 8 : i.type.bytes()));
            }

            arg_is_buffer.push_back(cast<uint8_t>(i.is_buffer));
        }

        // nullptr-terminate the lists
        args.push_back(reinterpret(Handle(), cast<uint64_t>(0)));
        if (runtime_run_takes_types) {
            internal_assert(halide_type_int == 0);
            arg_types_or_sizes.push_back(make_type_arg(Type(halide_type_int, 0, 0)));
        } else {
            arg_types_or_sizes.push_back(cast(target_size_t_type, 0));
        }
        arg_is_buffer.push_back(cast<uint8_t>(0));

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid[3]?
        internal_assert(is_const_one(bounds.num_threads[3]) && is_const_one(bounds.num_blocks[3]))
            << bounds.num_threads[3] << ", " << bounds.num_blocks[3] << "\n";
        debug(3) << "bounds.num_blocks[0] = " << bounds.num_blocks[0] << "\n";
        debug(3) << "bounds.num_blocks[1] = " << bounds.num_blocks[1] << "\n";
        debug(3) << "bounds.num_blocks[2] = " << bounds.num_blocks[2] << "\n";
        debug(3) << "bounds.num_threads[0] = " << bounds.num_threads[0] << "\n";
        debug(3) << "bounds.num_threads[1] = " << bounds.num_threads[1] << "\n";
        debug(3) << "bounds.num_threads[2] = " << bounds.num_threads[2] << "\n";

        string api_unique_name = gpu_codegen->api_unique_name();
        vector<Expr> run_args = {
            module_state(api_unique_name),
            kernel_name,
            Expr(bounds.num_blocks[0]),
            Expr(bounds.num_blocks[1]),
            Expr(bounds.num_blocks[2]),
            Expr(bounds.num_threads[0]),
            Expr(bounds.num_threads[1]),
            Expr(bounds.num_threads[2]),
            Expr(bounds.shared_mem_size),
            Call::make(Handle(), Call::make_struct, arg_types_or_sizes, Call::Intrinsic),
            Call::make(Handle(), Call::make_struct, args, Call::Intrinsic),
            Call::make(Handle(), Call::make_struct, arg_is_buffer, Call::Intrinsic),
        };
        return call_extern_and_assert("halide_" + api_unique_name + "_run", run_args);
    }

public:
    InjectGpuOffload(const Target &target)
        : target(target) {
        if (target.has_feature(Target::OpenGLCompute)) {
            cgdev[DeviceAPI::OpenGLCompute] = new_CodeGen_OpenGLCompute_Dev(target);
        }
        if (target.has_feature(Target::CUDA)) {
            cgdev[DeviceAPI::CUDA] = new_CodeGen_PTX_Dev(target);
        }
        if (target.has_feature(Target::OpenCL)) {
            cgdev[DeviceAPI::OpenCL] = new_CodeGen_OpenCL_Dev(target);
        }
        if (target.has_feature(Target::Metal)) {
            cgdev[DeviceAPI::Metal] = new_CodeGen_Metal_Dev(target);
        }
        if (target.has_feature(Target::D3D12Compute)) {
            cgdev[DeviceAPI::D3D12Compute] = new_CodeGen_D3D12Compute_Dev(target);
        }

        internal_assert(!cgdev.empty()) << "Requested unknown GPU target: " << target.to_string() << "\n";
    }

    Stmt inject(const Stmt &s) {
        // Create a new module for all of the kernels we find in this function.
        for (auto &i : cgdev) {
            i.second->init_module();
        }

        Stmt result = mutate(s);

        for (auto &i : cgdev) {
            string api_unique_name = i.second->api_unique_name();

            // If the module state for this API/function did not get created, there were
            // no kernels using this API.
            Expr state_ptr = module_state_ptr(api_unique_name, false);
            if (!state_ptr.defined()) {
                continue;
            }

            debug(2) << "Generating init_kernels for " << api_unique_name << "\n";
            vector<char> kernel_src = i.second->compile_to_src();
            Expr kernel_src_buf = make_buffer_ptr(kernel_src, api_unique_name + "_kernels");

            string init_kernels_name = "halide_" + api_unique_name + "_initialize_kernels";
            vector<Expr> init_args = {state_ptr, kernel_src_buf, Expr((int)kernel_src.size())};
            Stmt init_kernels = call_extern_and_assert(init_kernels_name, init_args);

            string destructor_name = "halide_" + api_unique_name + "_finalize_kernels";
            vector<Expr> finalize_args = {Expr(destructor_name), module_state(api_unique_name)};
            Stmt register_destructor = Evaluate::make(
                Call::make(Handle(), Call::register_destructor, finalize_args, Call::Intrinsic));

            result = Block::make({init_kernels, register_destructor, result});
        }
        return result;
    }
};

}  // namespace

Stmt inject_gpu_offload(Stmt s, const Target &host_target) {
    return InjectGpuOffload(host_target).inject(s);
}

}  // namespace Internal
}  // namespace Halide
