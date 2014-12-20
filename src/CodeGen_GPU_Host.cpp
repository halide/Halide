#include <sstream>

#include "CodeGen_GPU_Host.h"
#include "CodeGen_PTX_Dev.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_OpenGL_Dev.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "CodeGen_Internal.h"
#include "Util.h"
#include "Bounds.h"
#include "Simplify.h"

#ifdef _WIN32
#define NOMINMAX
#ifdef _WIN64
#define GPU_LIB_CC
#else
#define GPU_LIB_CC __stdcall
#endif
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(NULL), s) != NULL;
}
#else
#define GPU_LIB_CC
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(NULL, s) != NULL;
}
#endif

namespace Halide {
namespace Internal {

extern "C" { typedef struct CUctx_st *CUcontext; }

// A single global cuda context to share between jitted functions
int (GPU_LIB_CC *cuCtxDestroy)(CUctx_st *) = 0;

struct SharedCudaContext {
    CUctx_st *ptr;
    volatile int lock;

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0), lock(0) {
    }

    // Note that we never free the context, because static destructor
    // order is unpredictable, and we can't free the context before
    // all JITCompiledModules are freed. Users may be stashing Funcs
    // or Images in globals, and these keep JITCompiledModules around.
} cuda_ctx;

extern "C" {
    typedef struct cl_context_st *cl_context;
    typedef struct cl_command_queue_st *cl_command_queue;
}

int (GPU_LIB_CC *clReleaseContext)(cl_context);
int (GPU_LIB_CC *clReleaseCommandQueue)(cl_command_queue);

// A single global OpenCL context and command queue to share between jitted functions.
struct SharedOpenCLContext {
    cl_context context;
    cl_command_queue command_queue;
    volatile int lock;

    SharedOpenCLContext() : context(NULL), command_queue(NULL), lock(0) {
    }

    // We never free the context, for the same reason as above.
} cl_ctx;

using std::vector;
using std::string;
using std::map;

using namespace llvm;

// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the
// amount of shared memory to allocate.
class ExtractBounds : public IRVisitor {
public:

    Expr num_threads[4];
    Expr num_blocks[4];
    Expr shared_mem_size;

    ExtractBounds() : shared_mem_size(0), found_shared(false) {
        for (int i = 0; i < 4; i++) {
            num_threads[i] = num_blocks[i] = 1;
        }
    }

private:

    bool found_shared;
    Scope<Interval> scope;

    using IRVisitor::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            internal_assert(is_zero(op->min));
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

        if (!found_shared) {
            Interval ie = bounds_of_expr_in_scope(op->extent, scope);
            Interval im = bounds_of_expr_in_scope(op->min, scope);
            scope.push(op->name, Interval(im.min, im.max + ie.max - 1));
            op->body.accept(this);
            scope.pop(op->name);
        } else {
            op->body.accept(this);
        }
    }

    void visit(const Allocate *allocate) {
        if (allocate->name == "__shared") {
            internal_assert(allocate->type == UInt(8) && allocate->extents.size() == 1);
            shared_mem_size = bounds_of_expr_in_scope(allocate->extents[0], scope).max;
            found_shared = true;
        }
        allocate->body.accept(this);
    }

    void visit(const LetStmt *op) {
        if (!found_shared) {
            Interval i = bounds_of_expr_in_scope(op->value, scope);
            scope.push(op->name, i);
            op->body.accept(this);
            scope.pop(op->name);
        } else {
            op->body.accept(this);
        }
    }
};


class GPU_Host_Closure : public Halide::Internal::Closure {
public:
    GPU_Host_Closure(Stmt s, const std::string &lv, bool skip_gpu_loops=false) : skip_gpu_loops(skip_gpu_loops) {
        ignore.push(lv, 0);
        s.accept(this);
    }

    vector<GPU_Argument> arguments();

protected:
    using Internal::Closure::visit;

    void visit(const For *op);

    void visit(const Call *op) {
        if (op->call_type == Call::Intrinsic &&
            (op->name == Call::glsl_texture_load ||
             op->name == Call::glsl_texture_store)) {

            // The argument to the call is either a StringImm or a broadcasted
            // StringImm if this is part of a vectorized expression

            const StringImm* string_imm = op->args[0].as<StringImm>();
            if (!string_imm) {
                internal_assert(op->args[0].as<Broadcast>());
                string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
            }

            internal_assert(string_imm);

            string bufname = string_imm->value;
            BufferRef &ref = buffers[bufname];
            ref.type = op->type;

            if (op->name == Call::glsl_texture_load) {
                ref.read = true;
            } else if (op->name == Call::glsl_texture_store) {
                ref.write = true;
            }

            // The Func's name and the associated .buffer are mentioned in the
            // argument lists, but don't treat them as free variables.
            ignore.push(bufname, 0);
            ignore.push(bufname + ".buffer", 0);
            Internal::Closure::visit(op);
            ignore.pop(bufname + ".buffer");
            ignore.pop(bufname);
        } else {
            Internal::Closure::visit(op);
        }
    }

    bool skip_gpu_loops;
};

vector<GPU_Argument> GPU_Host_Closure::arguments() {
    vector<GPU_Argument> res;
    for (map<string, Type>::const_iterator iter = vars.begin(); iter != vars.end(); ++iter) {
        debug(2) << "var: " << iter->first << "\n";
        res.push_back(GPU_Argument(iter->first, false, iter->second));
    }
    for (map<string, BufferRef>::const_iterator iter = buffers.begin(); iter != buffers.end(); ++iter) {
        debug(2) << "buffer: " << iter->first << " " << iter->second.size;
        if (iter->second.read) debug(2) << " (read)";
        if (iter->second.write) debug(2) << " (write)";
        debug(2) << "\n";

        GPU_Argument arg(iter->first, true, iter->second.type, iter->second.size);
        arg.read = iter->second.read;
        arg.write = iter->second.write;
        res.push_back(arg);
    }
    return res;
}


void GPU_Host_Closure::visit(const For *loop) {
    if (skip_gpu_loops &&
        CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        return;
    }
    Internal::Closure::visit(loop);
}

template<typename CodeGen_CPU>
CodeGen_GPU_Host<CodeGen_CPU>::CodeGen_GPU_Host(Target target) :
    CodeGen_CPU(target),
    cgdev(NULL) {
}

template<typename CodeGen_CPU>
CodeGen_GPU_Dev* CodeGen_GPU_Host<CodeGen_CPU>::make_dev(Target t)
{
    if (t.has_feature(Target::CUDA)) {
        debug(1) << "Constructing CUDA device codegen\n";
        return new CodeGen_PTX_Dev(t);
    } else if (t.has_feature(Target::OpenCL)) {
        debug(1) << "Constructing OpenCL device codegen\n";
        return new CodeGen_OpenCL_Dev(t);
    } else if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Constructing OpenGL device codegen\n";
        return new CodeGen_OpenGL_Dev(t);
    } else {
        internal_error << "Requested unknown GPU target: " << t.to_string() << "\n";
        return NULL;
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const LoweredFunc *op) {

    internal_assert(cgdev == NULL) << "Internal device code generator already exists.\n";

    // Set up a new device code generator for the kernels we find in this function.
    cgdev = make_dev(target);
    cgdev->init_module();

    // Call the base implementation to create the function.
    CodeGen_CPU::visit(op);

    // Get the kernel source for any GPU loops we encountered in the function.
    std::vector<char> kernel_src = cgdev->compile_to_src();

    // Store the kernel source in the generated module.
    Value *kernel_src_ptr = CodeGen_CPU::create_constant_binary_blob(kernel_src, "halide_kernel_src_" + op->name);

    delete cgdev;
    cgdev = NULL;

    // Now, insert a call to halide_init_kernels at the entry of the
    // function we just generated, passing it a pointer to the kernel
    // source.

    // Remember the entry block so we can branch to it upon init success.
    BasicBlock *entry = &function->getEntryBlock();

    // Insert a new block to run initialization at the beginning of the function.
    BasicBlock *init_kernels_bb = BasicBlock::Create(*context, "init_kernels",
                                                     function, entry);
    builder->SetInsertPoint(init_kernels_bb);
    Value *user_context = get_user_context();
    Value *kernel_size = ConstantInt::get(i32, kernel_src.size());
    Value *init = module->getFunction("halide_init_kernels");
    internal_assert(init) << "Could not find function halide_init_kernels in initial module\n";
    Value *result = builder->CreateCall4(init, user_context,
                                         get_module_state(),
                                         kernel_src_ptr, kernel_size);
    Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
    CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_init_kernels");

    // Upon success, jump to the original entry.
    builder->CreateBr(entry);
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const Buffer *op) {
    // Unset constant flag for embedded image global variables
    GlobalVariable *global = module->getNamedGlobal(op->name() + ".buffer");
    global->setConstant(false);
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const For *loop) {
    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        // We're in the loop over innermost thread dimension
        debug(2) << "Kernel launch: " << loop->name << "\n";

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
        GPU_Host_Closure c(loop, loop->name);

        // compile the kernel
        string kernel_name = unique_name("kernel_" + loop->name, false);
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (!isalnum(kernel_name[i])) {
                kernel_name[i] = '_';
            }
        }

        vector<GPU_Argument> closure_args = c.arguments();
        for (size_t i = 0; i < closure_args.size(); i++) {
            if (closure_args[i].is_buffer && allocations.contains(closure_args[i].name)) {
                closure_args[i].size = allocations.get(closure_args[i].name).constant_bytes;
            }
        }

        cgdev->add_kernel(loop, kernel_name, closure_args);

        // get the actual name of the generated kernel for this loop
        kernel_name = cgdev->get_current_kernel_name();
        debug(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";
        Value *entry_name_str = builder->CreateGlobalStringPtr(kernel_name, "entry_name");

        llvm::Type *target_size_t_type = (target.bits == 32) ? i32 : i64;

        // build the kernel arguments array
        llvm::PointerType *arg_t = i8->getPointerTo(); // void*
        int num_args = (int)closure_args.size();

        // NULL-terminated list
        Value *gpu_args_arr =
            create_alloca_at_entry(ArrayType::get(arg_t, num_args+1),
                                   num_args+1,
                                   kernel_name + "_args");

        // NULL-terminated list of size_t's
        Value *gpu_arg_sizes_arr =
            create_alloca_at_entry(ArrayType::get(target_size_t_type, num_args+1),
                                   num_args+1,
                                   kernel_name + "_arg_sizes");

        for (int i = 0; i < num_args; i++) {
            // get the closure argument
            string name = closure_args[i].name;
            Value *val;

            if (closure_args[i].is_buffer) {
                // If it's a buffer, dereference the dev handle
                val = buffer_dev(sym_get(name + ".buffer"));
            } else {
                // Otherwise just look up the symbol
                val = sym_get(name);
            }

            // allocate stack space to mirror the closure element. It
            // might be in a register and we need a pointer to it for
            // the gpu args array.
            Value *ptr = builder->CreateAlloca(val->getType(), NULL, name+".stack");
            // store the closure value into the stack space
            builder->CreateStore(val, ptr);

            // store a void* pointer to the argument into the gpu_args_arr
            Value *bits = builder->CreateBitCast(ptr, arg_t);
            builder->CreateStore(bits,
                                 builder->CreateConstGEP2_32(gpu_args_arr, 0, i));

            // store the size of the argument
            int size_bits = (closure_args[i].is_buffer) ? target.bits : closure_args[i].type.bits;
            builder->CreateStore(ConstantInt::get(target_size_t_type, size_bits/8),
                                 builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, i));
        }
        // NULL-terminate the lists
        builder->CreateStore(ConstantPointerNull::get(arg_t),
                             builder->CreateConstGEP2_32(gpu_args_arr, 0, num_args));
        builder->CreateStore(ConstantInt::get(target_size_t_type, 0),
                             builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, num_args));

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid[3]?
        internal_assert(is_one(bounds.num_threads[3]) && is_one(bounds.num_blocks[3]));
        Value *launch_args[] = {
            get_user_context(),
            builder->CreateLoad(get_module_state()),
            entry_name_str,
            codegen(bounds.num_blocks[0]), codegen(bounds.num_blocks[1]), codegen(bounds.num_blocks[2]),
            codegen(bounds.num_threads[0]), codegen(bounds.num_threads[1]), codegen(bounds.num_threads[2]),
            codegen(bounds.shared_mem_size),
            builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, 0, "gpu_arg_sizes_ar_ref"),
            builder->CreateConstGEP2_32(gpu_args_arr, 0, 0, "gpu_args_arr_ref")
        };
        llvm::Function *dev_run_fn = module->getFunction("halide_dev_run");
        internal_assert(dev_run_fn) << "Could not find halide_dev_run in module\n";
        Value *result = builder->CreateCall(dev_run_fn, launch_args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_dev_run");
    } else {
        CodeGen_CPU::visit(loop);
    }
}

template<typename CodeGen_CPU>
Value *CodeGen_GPU_Host<CodeGen_CPU>::get_module_state() {
    GlobalVariable *module_state = module->getGlobalVariable("module_state", true);
    if (!module_state)
    {
        // Create a global variable to hold the module state
        PointerType *void_ptr_type = llvm::Type::getInt8PtrTy(*context);
        module_state = new GlobalVariable(*module, void_ptr_type,
                                          false, GlobalVariable::PrivateLinkage,
                                          ConstantPointerNull::get(void_ptr_type),
                                          "module_state");
        debug(4) << "Created device module state global variable\n";
    }
    return module_state;
}


// Force template instantiation for x86 and arm.
#ifdef WITH_X86
template class CodeGen_GPU_Host<CodeGen_X86>;
#endif

#if defined(WITH_ARM) || defined(WITH_AARCH64)
template class CodeGen_GPU_Host<CodeGen_ARM>;
#endif

#ifdef WITH_MIPS
template class CodeGen_GPU_Host<CodeGen_MIPS>;
#endif

#ifdef WITH_NATIVE_CLIENT
template class CodeGen_GPU_Host<CodeGen_PNaCl>;
#endif

}}
