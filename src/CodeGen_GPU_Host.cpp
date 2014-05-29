#include <sstream>

#include "CodeGen_GPU_Host.h"
#include "CodeGen_PTX_Dev.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_OpenGL_Dev.h"
#include "CodeGen_SPIR_Dev.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "CodeGen_Internal.h"
#include "Util.h"
#include "Bounds.h"
#include "Simplify.h"

#ifdef _MSC_VER
// TODO: This is untested
#define NOMINMAX
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(NULL), s) != NULL;
}
#else
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(NULL, s) != NULL;
}
#endif

namespace Halide {
namespace Internal {

extern "C" { typedef struct CUctx_st *CUcontext; }

// A single global cuda context to share between jitted functions
int (*cuCtxDestroy)(CUctx_st *) = 0;
struct SharedCudaContext {
    CUctx_st *ptr;
    volatile int lock;

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0), lock(0) {
    }

    // Freed on program exit
    ~SharedCudaContext() {
        if (ptr && cuCtxDestroy) {
            debug(1) << "Cleaning up cuda context: " << ptr << ", " << cuCtxDestroy << "\n";
            (*cuCtxDestroy)(ptr);
            ptr = 0;
        }
    }
} cuda_ctx;

extern "C" {
    typedef struct cl_context_st *cl_context;
    typedef struct cl_command_queue_st *cl_command_queue;
}

int (*clReleaseContext)(cl_context);
int (*clReleaseCommandQueue)(cl_command_queue);

// A single global OpenCL context and command queue to share between jitted functions.
struct SharedOpenCLContext {
    cl_context context;
    cl_command_queue command_queue;
    volatile int lock;

    SharedOpenCLContext() : context(NULL), command_queue(NULL), lock(0) {
    }

    // Release context and command queue on exit.
    ~SharedOpenCLContext() {
        if (command_queue && clReleaseCommandQueue) {
            debug(1) << "Cleaning up OpenCL command queue: " << command_queue << " , " << clReleaseCommandQueue << "\n";
            clReleaseCommandQueue(command_queue);
            command_queue = NULL;
        }
        if (context && clReleaseContext) {
            debug(1) << "Cleaning up OpenCL context: " << context << " , " << clReleaseContext << "\n";
            clReleaseContext(context);
            context = NULL;
        }
    }
} cl_ctx;

using std::vector;
using std::string;
using std::map;

using namespace llvm;


// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the max
// size of each allocation (so we know how much local and shared
// memory to allocate).
class ExtractBounds : public IRVisitor {
public:

    Expr thread_extent[4];
    Expr block_extent[4];
    map<string, Expr> shared_allocations;
    map<string, Expr> local_allocations;

    ExtractBounds(Stmt s) : inside_thread(false) {
        s.accept(this);
        for (int i = 0; i < 4; i++) {
            if (!thread_extent[i].defined()) {
                thread_extent[i] = 1;
            } else {
                thread_extent[i] = simplify(thread_extent[i]);
            }
            if (!block_extent[i].defined()) {
                block_extent[i] = 1;
            } else {
                block_extent[i] = simplify(block_extent[i]);
            }
        }
    }

private:

    bool inside_thread;
    Scope<Interval> scope;

    using IRVisitor::visit;

    Expr unify(Expr a, Expr b) {
        if (!a.defined()) return b;
        if (!b.defined()) return a;
        return Halide::max(a, b);
    }

    void visit(const For *loop) {
        // What's the largest the extent could be?
        Expr max_extent = bounds_of_expr_in_scope(loop->extent, scope).max;

        bool old_inside_thread = inside_thread;

        if (ends_with(loop->name, ".threadidx")) {
            thread_extent[0] = unify(thread_extent[0], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidy")) {
            thread_extent[1] = unify(thread_extent[1], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidz")) {
            thread_extent[2] = unify(thread_extent[2], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidw")) {
            thread_extent[3] = unify(thread_extent[3], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".blockidx")) {
            block_extent[0] = unify(block_extent[0], max_extent);
        } else if (ends_with(loop->name, ".blockidy")) {
            block_extent[1] = unify(block_extent[1], max_extent);
        } else if (ends_with(loop->name, ".blockidz")) {
            block_extent[2] = unify(block_extent[2], max_extent);
        } else if (ends_with(loop->name, ".blockidw")) {
            block_extent[3] = unify(block_extent[3], max_extent);
        }

        // What's the largest the loop variable could be?
        Expr max_loop = bounds_of_expr_in_scope(loop->min + loop->extent - 1, scope).max;
        Expr min_loop = bounds_of_expr_in_scope(loop->min, scope).min;

        scope.push(loop->name, Interval(min_loop, max_loop));

        // Recurse into the loop body
        loop->body.accept(this);

        scope.pop(loop->name);

        inside_thread = old_inside_thread;
    }

    void visit(const LetStmt *let) {
        Interval bounds = bounds_of_expr_in_scope(let->value, scope);
        scope.push(let->name, bounds);
        let->body.accept(this);
        scope.pop(let->name);
    }

    void visit(const Allocate *allocate) {
        map<string, Expr> &table = inside_thread ? local_allocations : shared_allocations;

        // We should only encounter each allocate once
        internal_assert(table.find(allocate->name) == table.end());

        Expr size;
        if (allocate->extents.size() == 0) {
            size = 1;
        } else {
            size = allocate->extents[0];
            // TODO: worry about overflow
            for (size_t i = 1; i < allocate->extents.size(); i++) {
                size *= allocate->extents[i];
            }
            size = simplify(size);
        }

        // What's the largest this allocation could be (in bytes)?
        Expr elements = bounds_of_expr_in_scope(size, scope).max;
        int bytes_per_element = allocate->type.bits/8;
        table[allocate->name] = simplify(elements * bytes_per_element);

        allocate->body.accept(this);
    }
};

class GPU_Host_Closure : public Halide::Internal::Closure {
public:
    static GPU_Host_Closure make(Stmt s, const std::string &lv, bool skip_gpu_loops=false) {
        GPU_Host_Closure c;
        c.skip_gpu_loops = skip_gpu_loops;
        c.ignore.push(lv, 0);
        s.accept(&c);
        return c;
    }

    vector<Argument> arguments();

protected:
    using Internal::Closure::visit;

    void visit(const For *op);

    void visit(const Call *op) {
        if (op->call_type == Call::Intrinsic &&
            (op->name == Call::glsl_texture_load ||
             op->name == Call::glsl_texture_store)) {
            internal_assert(op->args[0].as<StringImm>());
            string bufname = op->args[0].as<StringImm>()->value;
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

vector<Argument> GPU_Host_Closure::arguments() {
    vector<Argument> res;
    for (map<string, Type>::const_iterator iter = vars.begin(); iter != vars.end(); ++iter) {
        debug(2) << "var: " << iter->first << "\n";
        res.push_back(Argument(iter->first, false, iter->second));
    }
    for (map<string, BufferRef>::const_iterator iter = buffers.begin(); iter != buffers.end(); ++iter) {
        debug(2) << "buffer: " << iter->first;
        if (iter->second.read) debug(2) << " (read)";
        if (iter->second.write) debug(2) << " (write)";
        debug(2) << "\n";

        Argument arg(iter->first, true, iter->second.type);
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
bool CodeGen_GPU_Host<CodeGen_CPU>::lib_cuda_linked = false;

template<typename CodeGen_CPU>
CodeGen_GPU_Host<CodeGen_CPU>::CodeGen_GPU_Host(Target target) :
    CodeGen_CPU(target),
    dev_malloc_fn(NULL),
    dev_free_fn(NULL),
    copy_to_dev_fn(NULL),
    copy_to_host_fn(NULL),
    dev_run_fn(NULL),
    dev_sync_fn(NULL),
    cgdev(make_dev(target)) {
}

template<typename CodeGen_CPU>
CodeGen_GPU_Dev* CodeGen_GPU_Host<CodeGen_CPU>::make_dev(Target t)
{
    if (t.features & Target::CUDA) {
        debug(1) << "Constructing CUDA device codegen\n";
        return new CodeGen_PTX_Dev(t);
    } else if (t.features & Target::SPIR64) {
        debug(1) << "Constructing SPIR64 device codegen\n";
        return new CodeGen_SPIR_Dev(t, 64);
    } else if (t.features & Target::SPIR) {
        debug(1) << "Constructing SPIR device codegen\n";
        return new CodeGen_SPIR_Dev(t, 32);
    } else if (t.features & Target::OpenCL) {
        debug(1) << "Constructing OpenCL device codegen\n";
        return new CodeGen_OpenCL_Dev(t);
    } else if (t.features & Target::OpenGL) {
        debug(1) << "Constructing OpenGL device codegen\n";
        return new CodeGen_OpenGL_Dev();
    } else {
        internal_error << "Requested unknown GPU target: " << t.to_string() << "\n";
        return NULL;
    }
}

template<typename CodeGen_CPU>
CodeGen_GPU_Host<CodeGen_CPU>::~CodeGen_GPU_Host() {
    delete cgdev;
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::compile(Stmt stmt, string name,
                                            const vector<Argument> &args,
                                            const vector<Buffer> &images_to_embed) {

    init_module();

    // also set up the child codegenerator - this is set up once per
    // PTX_Host::compile, and reused across multiple PTX_Dev::compile
    // invocations for different kernels.
    cgdev->init_module();

    module = get_initial_module_for_target(target, context);

    // grab runtime helper functions
    dev_malloc_fn = module->getFunction("halide_dev_malloc");
    internal_assert(dev_malloc_fn) << "Could not find halide_dev_malloc in module\n";

    dev_free_fn = module->getFunction("halide_dev_free");
    internal_assert(dev_free_fn) << "Could not find halide_dev_free in module\n";

    copy_to_host_fn = module->getFunction("halide_copy_to_host");
    internal_assert(copy_to_host_fn) << "Could not find halide_copy_to_host in module\n";

    copy_to_dev_fn = module->getFunction("halide_copy_to_dev");
    internal_assert(copy_to_dev_fn) << "Could not find halide_copy_to_dev in module\n";

    dev_run_fn = module->getFunction("halide_dev_run");
    internal_assert(dev_run_fn) << "Could not find halide_dev_run in module\n";

    dev_sync_fn = module->getFunction("halide_dev_sync");
    internal_assert(dev_sync_fn) << "Could not find halide_dev_sync in module\n";

    // Fix the target triple
    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    llvm::Triple triple = CodeGen_CPU::get_target_triple();
    module->setTargetTriple(triple.str());

    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";


    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Unset constant flag for embedded image global variables
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        string name = images_to_embed[i].name();
        GlobalVariable *global = module->getNamedGlobal(name + ".buffer");
        global->setConstant(false);
    }

    std::vector<char> kernel_src = cgdev->compile_to_src();

    Value *kernel_src_ptr = CodeGen_CPU::create_constant_binary_blob(kernel_src, "halide_kernel_src");

    // Jump to the start of the function and insert a call to halide_init_kernels
    builder->SetInsertPoint(function->getEntryBlock().getFirstInsertionPt());
    Value *user_context = get_user_context();
    Value *kernel_size = ConstantInt::get(i32, kernel_src.size());
    Value *init = module->getFunction("halide_init_kernels");
    internal_assert(init) << "Could not find function halide_init_kernels in initial module\n";
    Value *state = builder->CreateCall4(init, user_context,
                                        builder->CreateLoad(get_module_state()),
                                        kernel_src_ptr, kernel_size);
    builder->CreateStore(state, get_module_state());

    // Optimize the module
    CodeGen::optimize_module();
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::jit_init(ExecutionEngine *ee, Module *module) {

    // Make sure extern cuda calls inside the module point to the
    // right things. If cuda is already linked in we should be
    // fine. If not we need to tell llvm to load it.
    if (target.features & Target::CUDA && !lib_cuda_linked) {
        // First check if libCuda has already been linked
        // in. If so we shouldn't need to set any mappings.
        if (have_symbol("cuInit")) {
            debug(1) << "This program was linked to cuda already\n";
        } else {
            debug(1) << "Looking for cuda shared library...\n";
            string error;
            llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.so", &error);
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.dylib", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/Library/Frameworks/CUDA.framework/CUDA", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("nvcuda.dll", &error);
            }
            user_assert(error.empty()) << "Could not find libcuda.so, libcuda.dylib, or nvcuda.dll\n";
        }
        lib_cuda_linked = true;

        // Now dig out cuCtxDestroy_v2 so that we can clean up the
        // shared context at termination
        void *ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("cuCtxDestroy_v2");
        internal_assert(ptr) << "Could not find cuCtxDestroy_v2 in cuda library\n";

        cuCtxDestroy = reinterpret_bits<int (*)(CUctx_st *)>(ptr);

    } else if (target.features & Target::OpenCL) {
        // First check if libOpenCL has already been linked
        // in. If so we shouldn't need to set any mappings.
        if (have_symbol("clCreateContext")) {
            debug(1) << "This program was linked to OpenCL already\n";
        } else {
            debug(1) << "Looking for OpenCL shared library...\n";
            string error;
            llvm::sys::DynamicLibrary::LoadLibraryPermanently("libOpenCL.so", &error);
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/OpenCL.framework/OpenCL", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("opencl.dll", &error); // TODO: test on Windows
            }
            user_assert(error.empty()) << "Could not find libopencl.so, OpenCL.framework, or opencl.dll\n";
        }

        // Now dig out clReleaseContext/CommandQueue so that we can clean up the
        // shared context at termination
        void *ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("clReleaseContext");
        internal_assert(ptr) << "Could not find clReleaseContext\n";

        clReleaseContext = reinterpret_bits<int (*)(cl_context)>(ptr);

        ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("clReleaseCommandQueue");
        internal_assert(ptr) << "Could not find clReleaseCommandQueue\n";

        clReleaseCommandQueue = reinterpret_bits<int (*)(cl_command_queue)>(ptr);

    } else if (target.features & Target::OpenGL) {
        if (target.os == Target::Linux) {
            if (have_symbol("glXGetCurrentContext") && have_symbol("glDeleteTextures")) {
                debug(1) << "OpenGL support code already linked in...\n";
            } else {
                debug(1) << "Looking for OpenGL support code...\n";
                string error;
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libGL.so.1", &error);
                user_assert(error.empty()) << "Could not find libGL.so\n";
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libX11.so", &error);
                user_assert(error.empty()) << "Could not find libX11.so\n";
            }
        } else if (target.os == Target::OSX) {
            if (have_symbol("aglCreateContext") && have_symbol("glDeleteTextures")) {
                debug(1) << "OpenGL support code already linked in...\n";
            } else {
                debug(1) << "Looking for OpenGL support code...\n";
                string error;
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/AGL.framework/AGL", &error);
                user_assert(error.empty()) << "Could not find AGL.framework\n";
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/OpenGL.framework/OpenGL", &error);
                user_assert(error.empty()) << "Could not find OpenGL.framework\n";
            }
        } else {
            internal_error << "JIT support for OpenGL on anything other than linux or OS X not yet implemented\n";
        }
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::jit_finalize(ExecutionEngine *ee, Module *module,
                                                 vector<JITCompiledModule::CleanupRoutine> *cleanup_routines) {
    if (target.features & Target::CUDA) {
        // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
        // CUDA behaves much better when you don't initialize >2 contexts.
        llvm::Function *fn = module->getFunction("halide_set_cuda_context");
        internal_assert(fn) << "Could not find halide_set_cuda_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cuda_context in module\n";
        void (*set_cuda_context)(CUcontext *, volatile int *) =
            reinterpret_bits<void (*)(CUcontext *, volatile int *)>(f);
        set_cuda_context(&cuda_ctx.ptr, &cuda_ctx.lock);
    } else if (target.features & Target::OpenCL) {
        // Share the same cl_ctx, cl_q across all OpenCL modules.
        llvm::Function *fn = module->getFunction("halide_set_cl_context");
        internal_assert(fn) << "Could not find halide_set_cl_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cl_context in module\n";
        void (*set_cl_context)(cl_context *, cl_command_queue *, volatile int *) =
            reinterpret_bits<void (*)(cl_context *, cl_command_queue *, volatile int *)>(f);
        set_cl_context(&cl_ctx.context, &cl_ctx.command_queue, &cl_ctx.lock);
    }

    // If the module contains a halide_release function, run it when the module dies.
    llvm::Function *fn = module->getFunction("halide_release");
    if (fn) {
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_release in module\n";
        void (*cleanup_routine)(void *) =
            reinterpret_bits<void (*)(void *)>(f);
        cleanup_routines->push_back(JITCompiledModule::CleanupRoutine(cleanup_routine, NULL));
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const For *loop) {
    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        debug(2) << "Kernel launch: " << loop->name << "\n";

        // compute kernel launch bounds
        ExtractBounds bounds(loop);

        Expr n_tid_x   = bounds.thread_extent[0];
        Expr n_tid_y   = bounds.thread_extent[1];
        Expr n_tid_z   = bounds.thread_extent[2];
        Expr n_tid_w   = bounds.thread_extent[3];
        Expr n_blkid_x = bounds.block_extent[0];
        Expr n_blkid_y = bounds.block_extent[1];
        Expr n_blkid_z = bounds.block_extent[2];
        Expr n_blkid_w = bounds.block_extent[3];
        debug(2) << "Kernel bounds: ("
               << n_tid_x << ", " << n_tid_y << ", " << n_tid_z << ", " << n_tid_w << ") threads, ("
               << n_blkid_x << ", " << n_blkid_y << ", " << n_blkid_z << ", " << n_blkid_w << ") blocks\n";

        // compute a closure over the state passed into the kernel
        GPU_Host_Closure c = GPU_Host_Closure::make(loop, loop->name);

        // Note that we currently do nothing with the thread-local
        // allocations found. Currently we only handle const-sized
        // ones, and we handle those by generating allocas at the top
        // of the device kernel.

        // Compute and pass in offsets into shared memory for all the internal allocations
        Expr n_threads = n_tid_x * n_tid_y * n_tid_z * n_tid_w;
        Value *shared_mem_size = ConstantInt::get(i32, 0);
        vector<string> shared_mem_allocations;
        for (map<string, Expr>::iterator iter = bounds.shared_allocations.begin();
             iter != bounds.shared_allocations.end(); ++iter) {

            // TODO: Might offsets into shared memory need to be
            // aligned? What if it's OpenCL and the kernel does vector
            // loads?
            debug(2) << "Internal shared allocation" << iter->first
                     << " has max size " << iter->second << "\n";

            Value *size = codegen(iter->second);

            string name = iter->first + ".shared_mem";
            shared_mem_allocations.push_back(name);
            sym_push(name, shared_mem_size);
            c.vars[name] = Int(32);

            shared_mem_size = builder->CreateAdd(shared_mem_size, size);
        }

        // compile the kernel
        string kernel_name = unique_name("kernel_" + loop->name, false);
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (!isalnum(kernel_name[i])) {
                kernel_name[i] = '_';
            }
        }
        cgdev->add_kernel(loop, kernel_name, c.arguments());

        // get the actual name of the generated kernel for this loop
        kernel_name = cgdev->get_current_kernel_name();
        debug(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";
        Value *entry_name_str = builder->CreateGlobalStringPtr(kernel_name, "entry_name");

        llvm::Type *target_size_t_type = (target.bits == 32) ? i32 : i64;

        // build the kernel arguments array
        vector<Argument> closure_args = c.arguments();
        llvm::PointerType *arg_t = i8->getPointerTo(); // void*
        int num_args = (int)closure_args.size();

        Value *gpu_args_arr =
            create_alloca_at_entry(ArrayType::get(arg_t, num_args+1), // NULL-terminated list
                                   num_args+1,
                                   kernel_name + "_args");

        Value *gpu_arg_sizes_arr =
            create_alloca_at_entry(ArrayType::get(target_size_t_type, num_args+1), // NULL-terminated list of size_t's
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

        // Figure out how much shared memory we need to allocate, and
        // build offsets into it for the internal allocations

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid_w?
        Value *launch_args[] = {
            get_user_context(),
            builder->CreateLoad(get_module_state()),
            entry_name_str,
            codegen(n_blkid_x), codegen(n_blkid_y), codegen(n_blkid_z),
            codegen(n_tid_x), codegen(n_tid_y), codegen(n_tid_z),
            shared_mem_size,
            builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, 0, "gpu_arg_sizes_ar_ref"),
            builder->CreateConstGEP2_32(gpu_args_arr, 0, 0, "gpu_args_arr_ref")
        };
        Value *result = builder->CreateCall(dev_run_fn, launch_args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_dev_run");

        // Pop the shared memory allocations from the symbol table
        for (size_t i = 0; i < shared_mem_allocations.size(); i++) {
            sym_pop(shared_mem_allocations[i]);
        }
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

#ifdef WITH_ARM
template class CodeGen_GPU_Host<CodeGen_ARM>;
#endif


}}
