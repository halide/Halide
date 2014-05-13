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

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0) {
    }

    // Freed on program exit
    ~SharedCudaContext() {
        debug(1) << "Cleaning up cuda context: " << ptr << ", " << cuCtxDestroy << "\n";
        if (ptr && cuCtxDestroy) {
            (*cuCtxDestroy)(ptr);
            ptr = 0;
        }
    }
} cuda_ctx;

// A single global OpenCL context and command queue to share among modules.
void * cl_ctx = 0;
void * cl_q = 0;

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

// Is a buffer ever used on the host? Used on the device? Determines
// whether we need to allocate memory for each. (TODO: consider
// debug_to_file on host of a buffer only used on device)
class WhereIsBufferUsed : public IRVisitor {
public:
    string buf;
    bool used_on_host, used_on_device,
        written_on_host, read_on_host,
        written_on_device, read_on_device,
        has_buffer_defined;
    WhereIsBufferUsed(string b) : buf(b),
                                  used_on_host(false),
                                  used_on_device(false),
                                  written_on_host(false),
                                  read_on_host(false),
                                  written_on_device(false),
                                  read_on_device(false),
                                  in_device_code(false) {}


private:
    bool in_device_code;

    using IRVisitor::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            op->min.accept(this);
            op->extent.accept(this);
            bool old_in_device = in_device_code;
            in_device_code = true;
            op->body.accept(this);
            in_device_code = old_in_device;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) {
        read_access(op->name);
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        write_access(op->name);
        IRVisitor::visit(op);
    }

    void read_access(const string &name) {
        if (name == buf) {
            if (in_device_code) {
                used_on_device = true;
                read_on_device = true;
            } else {
                used_on_host = true;
                read_on_host = true;
            }
        }
    }

    void write_access(const string &name) {
        if (name == buf) {
            if (in_device_code) {
                used_on_device = true;
                written_on_device = true;
            } else {
                used_on_host = true;
                written_on_host = true;
            }
        }
    }

    void visit(const Call *op) {
        if (op->call_type == Call::Intrinsic && op->name == Call::glsl_texture_load) {
            internal_assert(op->args.size() == 5);
            internal_assert(op->args[0].as<StringImm>());
            string bufname = op->args[0].as<StringImm>()->value;
            read_access(bufname);
        } else if (op->call_type == Call::Intrinsic && op->name == Call::glsl_texture_store) {
            internal_assert(op->args.size() == 6);
            internal_assert(op->args[0].as<StringImm>());
            string bufname = op->args[0].as<StringImm>()->value;
            write_access(bufname);
        }
        IRVisitor::visit(op);
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
void CodeGen_GPU_Host<CodeGen_CPU>::jit_finalize(ExecutionEngine *ee, Module *module, vector<void (*)()> *cleanup_routines) {
    if (target.features & Target::CUDA) {
        // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
        // CUDA behaves much better when you don't initialize >2 contexts.
        llvm::Function *fn = module->getFunction("halide_set_cuda_context");
        internal_assert(fn) << "Could not find halide_set_cuda_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cuda_context in module\n";
        void (*set_cuda_context)(CUcontext *) =
            reinterpret_bits<void (*)(CUcontext *)>(f);
        set_cuda_context(&cuda_ctx.ptr);

        // When the module dies, we need to call halide_release
        fn = module->getFunction("halide_release");
        internal_assert(fn) << "Could not find halide_release in module\n";
        f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_release in module\n";
        void (*cleanup_routine)() =
            reinterpret_bits<void (*)()>(f);
        cleanup_routines->push_back(cleanup_routine);
    } else if (target.features & Target::OpenCL) {
        // Share the same cl_ctx, cl_q across all OpenCL modules.
        llvm::Function *fn = module->getFunction("halide_set_cl_context");
        internal_assert(fn) << "Could not find halide_set_cl_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cl_context in module\n";
        void (*set_cl_context)(void **, void **) =
            reinterpret_bits<void (*)(void **, void **)>(f);
        set_cl_context(&cl_ctx, &cl_q);

        // When the module dies, we need to call halide_release
        fn = module->getFunction("halide_release");
        internal_assert(fn) << "Could not find halide_release in module\n";
        f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_release in module\n";
        void (*cleanup_routine)() =
            reinterpret_bits<void (*)()>(f);
        cleanup_routines->push_back(cleanup_routine);
    } else if (target.features & Target::OpenGL) {
        llvm::Function *fn = module->getFunction("halide_release");
        internal_assert(fn) << "Could not find halide_release in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_release in module\n";
        void (*cleanup_routine)() =
            reinterpret_bits<void (*)()>(f);
        cleanup_routines->push_back(cleanup_routine);
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

        map<string, Closure::BufferRef>::iterator it;
        for (it = c.buffers.begin(); it != c.buffers.end(); ++it) {
            // While internal buffers have all had their device
            // allocations done via static analysis, external ones
            // need to be dynamically checked
            Value *user_context = get_user_context();
            Value *buf = sym_get(it->first + ".buffer");
            debug(4) << "halide_dev_malloc " << it->first << "\n";
            Value *result = builder->CreateCall2(dev_malloc_fn, user_context, buf);
            Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
            CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_dev_malloc");

            // Anything dirty on the cpu that gets read on the gpu
            // needs to be copied over
            if (it->second.read) {
                debug(4) << "halide_copy_to_dev " << it->first << "\n";
                Value *result = builder->CreateCall2(copy_to_dev_fn, user_context, buf);
                Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
                CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_copy_to_dev");
            }
        }

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

        // mark written buffers dirty
        for (it = c.buffers.begin(); it != c.buffers.end(); ++it) {
            if (it->second.write) {
                debug(4) << "setting dev_dirty " << it->first << " (write)\n";
                Value *buf = sym_get(it->first + ".buffer");
                builder->CreateStore(ConstantInt::get(i8, 1),
                                     buffer_dev_dirty_ptr(buf));

                // If host was still dirty we must have clobbered it,
                // so it's not dirty now.
                builder->CreateStore(ConstantInt::get(i8, 0),
                                     buffer_host_dirty_ptr(buf));
            }
        }

        // Pop the shared memory allocations from the symbol table
        for (size_t i = 0; i < shared_mem_allocations.size(); i++) {
            sym_pop(shared_mem_allocations[i]);
        }
    } else {
        CodeGen_CPU::visit(loop);
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const Allocate *alloc) {
    WhereIsBufferUsed usage(alloc->name);
    alloc->accept(&usage);

    typename CodeGen_CPU::Allocation host_allocation = {NULL, 0};

    if (usage.used_on_host) {
        debug(2) << alloc->name << " is used on the host\n";
        host_allocation = create_allocation(alloc->name, alloc->type, alloc->extents);
        sym_push(alloc->name + ".host", host_allocation.ptr);
    } else {
        host_allocation.ptr = ConstantPointerNull::get(llvm_type_of(alloc->type)->getPointerTo());
    }

    Value *buf = NULL;
    bool should_pop = false;
    if (usage.used_on_device) {
        debug(2) << alloc->name << " is used on the device\n";

        buf = sym_get(alloc->name + ".buffer", false);
        if (!buf) {
            buf = create_alloca_at_entry(buffer_t_type, 1);
            Value *zero32 = ConstantInt::get(i32, 0),
                *one32  = ConstantInt::get(i32, 1),
                *null64 = ConstantInt::get(i64, 0),
                *zero8  = ConstantInt::get( i8, 0);

            Value *host_ptr = builder->CreatePointerCast(host_allocation.ptr, i8->getPointerTo());
            builder->CreateStore(host_ptr, buffer_host_ptr(buf));
            builder->CreateStore(null64,   buffer_dev_ptr(buf));

            builder->CreateStore(zero8,  buffer_host_dirty_ptr(buf));
            builder->CreateStore(zero8,  buffer_dev_dirty_ptr(buf));

            Value *llvm_size;
            int32_t constant_size;
            if (constant_allocation_size(alloc->extents, alloc->name, constant_size)) {
                int64_t size_in_bytes = (int64_t)constant_size * alloc->type.bytes();
                if (size_in_bytes > ((int64_t(1) << 31) - 1)) {
                    user_error
                        << "Total size for GPU allocation " << alloc->name
                        << " is constant (" << size_in_bytes << ") but exceeds 2^31 - 1.";
                    llvm_size = NULL;
                } else {
                    // Size in elements, not size in bytes.
                    llvm_size = ConstantInt::get(i32, constant_size);
                }
            } else {
                // Note we compute this using a type of size 1, because we
                // want the size in elements, not in bytes.
                llvm_size = codegen_allocation_size(alloc->name, UInt(8), alloc->extents);
            }

            // For now, we just track the allocation as a single 1D buffer of the
            // required size. If we break this into multiple dimensions, this will
            // fail to account for expansion for alignment.
            builder->CreateStore(llvm_size,
                                 buffer_extent_ptr(buf, 0));
            builder->CreateStore(zero32,  buffer_extent_ptr(buf, 1));
            builder->CreateStore(zero32,  buffer_extent_ptr(buf, 2));
            builder->CreateStore(zero32,  buffer_extent_ptr(buf, 3));

            builder->CreateStore(one32,   buffer_stride_ptr(buf, 0));
            builder->CreateStore(zero32,  buffer_stride_ptr(buf, 1));
            builder->CreateStore(zero32,  buffer_stride_ptr(buf, 2));
            builder->CreateStore(zero32,  buffer_stride_ptr(buf, 3));

            builder->CreateStore(zero32,  buffer_min_ptr(buf, 0));
            builder->CreateStore(zero32,  buffer_min_ptr(buf, 1));
            builder->CreateStore(zero32,  buffer_min_ptr(buf, 2));
            builder->CreateStore(zero32,  buffer_min_ptr(buf, 3));

            int bytes = alloc->type.width * alloc->type.bytes();
            builder->CreateStore(ConstantInt::get(i32, bytes),
                                 buffer_elem_size_ptr(buf));
            sym_push(alloc->name + ".buffer", buf);
            should_pop = true;
        } else {
            Value *host_ptr = builder->CreatePointerCast(host_allocation.ptr, i8->getPointerTo());
            builder->CreateStore(host_ptr, buffer_host_ptr(buf));
        }

        Value *args[2] = { get_user_context(), buf };
        Value *result = builder->CreateCall(dev_malloc_fn, args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_dev_malloc");

        // Put the dev pointer in the symbol table. Mostly so we
        // remember to dev_free it.
        sym_push(alloc->name + ".dev", buffer_dev(buf));
    }

    codegen(alloc->body);

    if (usage.used_on_host) {
        destroy_allocation(host_allocation);
    }

    if (should_pop) {
        sym_pop(alloc->name + ".buffer");
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const Free *f) {
    // Free any host allocation
    if (sym_exists(f->name + ".host")) {
        CodeGen_CPU::visit(f);
    }

    if (sym_exists(f->name + ".dev")) {
        Value *args[2] = { get_user_context(),
                           sym_get(f->name + ".buffer") };
        Value *result = builder->CreateCall(dev_free_fn, args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_dev_free");
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const Pipeline *n) {

    // Copying to the gpu and marking gpu_dirty is handled by the For
    // handler above, because that marks a good test for when we enter
    // kernels. Here we need to handle copying back to the CPU if
    // needed.

    // Are we tracking a buffer for this allocation?
    Value *buf = sym_get(n->name + ".buffer", false);

    // Is it also used on the host?
    Value *host = sym_get(n->name + ".host", false);

    // If we didn't find anything, maybe it's a tuple. Go looking for
    // all the tuple elements.
    vector<Value *> bufs;
    if (!buf) {
        for (int i = 0; ; i++) {
            string name = n->name + "." + int_to_string(i);
            buf = sym_get(name + ".buffer", false);
            host = sym_get(name + ".host", false);
            if (!buf) break;
            if (host) {
                // Skip the ones not used on the host
                bufs.push_back(buf);
            }
        }
    } else if (host) {
        bufs.push_back(buf);
    }

    if (bufs.empty()) {
        CodeGen_CPU::visit(n);
        return;
    }

    vector<WhereIsBufferUsed> produce_usage;
    for (size_t i = 0; i < bufs.size(); i++) {
        string name = n->name;
        if (bufs.size() > 1) name += "." + int_to_string(i);
        WhereIsBufferUsed u(name);
        n->produce.accept(&u);
        produce_usage.push_back(u);
    }

    codegen(n->produce);

    // Track host writes
    for (size_t i = 0; i < bufs.size(); i++) {
        if (produce_usage[i].written_on_host) {
            builder->CreateStore(ConstantInt::get(i8, 1),
                                 buffer_host_dirty_ptr(bufs[i]));
            // A produce node clobbers the entire buffer, so if this
            // was somehow dirty on the GPU, it isn't anymore (which
            // may happen if we're producing into a buffer_t passed in
            // that's dirty on the GPU).
            builder->CreateStore(ConstantInt::get(i8, 0),
                                 buffer_dev_dirty_ptr(bufs[i]));
        }
    }

    Value *user_context = get_user_context();
    if (n->update.defined()) {

        // Extract all the update steps (TODO: Pipeline nodes should
        // really just have a list of update steps instead of using
        // blocks).
        vector<Stmt> steps;
        vector<Stmt> stack;
        stack.push_back(n->update);
        while (!stack.empty()) {
            Stmt s = stack.back();
            stack.pop_back();
            if (const Block *b = s.as<Block>()) {
                stack.push_back(b->rest);
                stack.push_back(b->first);
            } else {
                steps.push_back(s);
            }
        }

        // For each update step
        for (size_t j = 0; j < steps.size(); j++) {
            Stmt s = steps[j];

            vector<WhereIsBufferUsed> update_usage;
            for (size_t i = 0; i < bufs.size(); i++) {
                string name = n->name;
                if (bufs.size() > 1) name += "." + int_to_string(i);
                WhereIsBufferUsed u(name);
                s.accept(&u);
                update_usage.push_back(u);
            }

            // Copy back host update accesses
            for (size_t i = 0; i < bufs.size(); i++) {
                // We need to copy back buffers that the host will
                // write to as well, because the update step may not
                // write to *all* of the buffer, and we don't want to
                // get into a situation where different parts of the
                // buffer are dirty on host and device. This could
                // theoretically be skipped if this update definition
                // is pure, but we've lost that metadata at this stage
                // of codegen.
                if (update_usage[i].used_on_host) {
                    // debug(0) << "Before update step " << j << " copy tuple element " << i << " to host\n";
                    Value *result = builder->CreateCall2(copy_to_host_fn, user_context, bufs[i]);
                    Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
                    CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_copy_to_host");
                }
            }

            codegen(s);

            // Track host update writes
            for (size_t i = 0; i < bufs.size(); i++) {
                if (update_usage[i].written_on_host) {
                    builder->CreateStore(ConstantInt::get(i8, 1),
                                         buffer_host_dirty_ptr(bufs[i]));
                }
            }
        }

    }


    // Copy back host reads before consume

    vector<WhereIsBufferUsed> consume_usage;
    for (size_t i = 0; i < bufs.size(); i++) {
        string name = n->name;
        if (bufs.size() > 1) name += "." + int_to_string(i);
        WhereIsBufferUsed u(name);
        n->consume.accept(&u);
        consume_usage.push_back(u);
    }

    for (size_t i = 0; i < bufs.size(); i++) {
        if (consume_usage[i].read_on_host) {
            // debug(0) << "Before consume step copy tuple element " << i << " to host\n";
            Value *result = builder->CreateCall2(copy_to_host_fn, user_context, bufs[i]);
            Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
            CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_copy_to_host");
        }
    }

    codegen(n->consume);
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::visit(const Call *call) {
    // The other way in which buffers might be read by the host is in
    // calls to whole-buffer builtins like debug_to_file.
    if (call->call_type == Call::Intrinsic &&
        call->name == Call::debug_to_file) {
        internal_assert(call->args.size() == 9) << "malformed debug to file node\n";
        const Load *func = call->args[1].as<Load>();
        internal_assert(func) << "malformed debug to file node\n";

        Value *buf = sym_get(func->name + ".buffer", false);
        if (buf) {
            // This buffer may have been last-touched on device
            Value *user_context = get_user_context();
            Value *result = builder->CreateCall2(copy_to_host_fn, user_context, buf);
            Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
            CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_copy_to_host");
        }
        // fall through
    }

    CodeGen::visit(call);
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
