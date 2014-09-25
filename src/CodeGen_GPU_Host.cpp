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
#include "VaryingAttributes.h"

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
bool CodeGen_GPU_Host<CodeGen_CPU>::lib_cuda_linked = false;

template<typename CodeGen_CPU>
CodeGen_GPU_Host<CodeGen_CPU>::CodeGen_GPU_Host(Target target) :
    CodeGen_CPU(target),
    cgdev(make_dev(target)) {
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

    // Remember the entry block so we can branch to it upon init success.
    BasicBlock *entry = &function->getEntryBlock();

    // Insert a new block to run initialization at the beginning of the function.
    BasicBlock *init_kernels_bb = BasicBlock::Create(*context, "init_kernels",
                                                     function, entry);
    builder->SetInsertPoint(init_kernels_bb);
    Value *user_context = get_user_context();
    Value *kernel_size = ConstantInt::get(i32, kernel_src.size());
    llvm::Function *init = module->getFunction("halide_init_kernels");
    internal_assert(init) << "Could not find function halide_init_kernels in initial module\n";
  
    // Make the function extern so that the JIT'ed code will call the debuggable
    // version in the app
    // TODO: Can this operation be specified in the target?
    init->deleteBody();
  
    Value *result = builder->CreateCall4(init, user_context,
                                         get_module_state(),
                                         kernel_src_ptr, kernel_size);
    Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
    CodeGen_CPU::create_assertion(did_succeed, "Failure inside halide_init_kernels");

    // Upon success, jump to the original entry.
    builder->CreateBr(entry);

    // Optimize the module
    CodeGen::optimize_module();
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::jit_init(ExecutionEngine *ee, Module *module) {

    // Make sure extern cuda calls inside the module point to the
    // right things. If cuda is already linked in we should be
    // fine. If not we need to tell llvm to load it.
    if (target.has_feature(Target::CUDA) && !lib_cuda_linked) {
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

        cuCtxDestroy = reinterpret_bits<int (GPU_LIB_CC *)(CUctx_st *)>(ptr);

    } else if (target.has_feature(Target::OpenCL)) {
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

        clReleaseContext = reinterpret_bits<int (GPU_LIB_CC *)(cl_context)>(ptr);

        ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("clReleaseCommandQueue");
        internal_assert(ptr) << "Could not find clReleaseCommandQueue\n";

        clReleaseCommandQueue = reinterpret_bits<int (GPU_LIB_CC *)(cl_command_queue)>(ptr);

    } else if (target.has_feature(Target::OpenGL)) {
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
    if (target.has_feature(Target::CUDA)) {
        // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
        // CUDA behaves much better when you don't initialize >2 contexts.
        llvm::Function *fn = module->getFunction("halide_set_cuda_context");
        internal_assert(fn) << "Could not find halide_set_cuda_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cuda_context in module\n";
        void (*set_cuda_context)(CUcontext *, volatile int *) =
            reinterpret_bits<void (*)(CUcontext *, volatile int *)>(f);
        set_cuda_context(&cuda_ctx.ptr, &cuda_ctx.lock);
    } else if (target.has_feature(Target::OpenCL)) {
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
    CodeGen_CPU::jit_finalize(ee, module, cleanup_routines);
}

/** Given an expression for a spatial coordinate and the maximum spatial 
 *  coordinate in its dimension, this function returns an expression for the
 *  GL device coordinates of the original expression.
 */
Expr deviceCoordinates(Expr v, Expr max_dim) {

    if (v.type() != Float(32)) {
        v = Cast::make(Float(32),v);
    }
    
    if (max_dim.type() != Float(32)) {
        max_dim = Cast::make(Float(32),max_dim);
    }
    
    return Sub::make(Mul::make(Div::make(Cast::make(Float(32),v), Cast::make(Float(32),max_dim)),2.0f),1.0f);
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

        // compile the kernel
        string kernel_name = unique_name("kernel_" + loop->name, false);
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (!isalnum(kernel_name[i])) {
                kernel_name[i] = '_';
            }
        }
        
        Value* null_ptr_value = ConstantPointerNull::get(llvm::Type::getInt8PtrTy(*context));
        Value* gpu_attribute_names = null_ptr_value;
        Value* gpu_num_attributes  = null_ptr_value;
        Value* gpu_coords_per_dim  = null_ptr_value;
        Value* gpu_num_coords_dim0 = null_ptr_value;
        Value* gpu_num_coords_dim1 = null_ptr_value;
        
        std::map<std::string,Expr> varying;
        
        Stmt loop_stmt;
        
        if (target.features & Target::OpenGL) {
            
            // GL draw calls that invoke the GLSL shader are issued for pairs of
            // for-loops over spatial x and y dimensions. For each for-loop we create
            // one scalar vertex attribute for the spatial dimension corresponding to
            // that loop, plus one scalar attribute for each Let expression previously
            // labeled as ".varying"
            
            ExpressionMesh mesh;
            loop_stmt = setup_mesh(loop,mesh,varying);
            
            // Create an array of null terminated strings containing the attribute
            // names in the order they appear per vertex channel
            int num_attributes = mesh.attributes.size();
            
            gpu_attribute_names = create_alloca_at_entry(ArrayType::get(llvm::Type::getInt8PtrTy(*context), num_attributes),
                                                         num_attributes,
                                                         kernel_name + "_attribute_names");
            
            for (int i=0;i!=num_attributes;++i) {
                
                CodeGen_GLSL* glsl = dynamic_cast<CodeGen_OpenGL_Dev*>(cgdev)->glc;
                std::string name = glsl->print_name(mesh.attributes[i] + "_attrib");
                std::string mangled = replace_all(name, "__", "XX");
                
                Value* gpu_attribute = CodeGen::create_string_constant(mangled);
                builder->CreateStore(gpu_attribute, builder->CreateConstGEP2_64(gpu_attribute_names, 0, i));
            }
            
            // Record the number of attributes
            gpu_num_attributes = codegen(IntImm::make(num_attributes));
            
            // Record the expressions for each dimension
            int num_coords_dim0 = mesh.coords[0].size();
            int num_coords_dim1 = mesh.coords[1].size();
            
            gpu_num_coords_dim0 = codegen(IntImm::make(num_coords_dim0));
            gpu_num_coords_dim1 = codegen(IntImm::make(num_coords_dim1));
            
            gpu_coords_per_dim = create_alloca_at_entry(ArrayType::get(llvm::Type::getFloatPtrTy(*context), num_attributes),
                                                        num_attributes,
                                                        kernel_name + "_coords_per_dim");
            
            for (int i=0;i!=num_attributes;++i) {
                
                // Create an array of coordinates for this dimension
                int num_coords = mesh.coords[i].size();
                Value* gpu_coords = create_alloca_at_entry(ArrayType::get(CodeGen::f32, num_coords),
                                                           num_coords,
                                                           kernel_name + "_coords_" + mesh.attributes[i]);
                for (int c=0;c!=num_coords;++c) {
                    Expr value = mesh.coords[i][c];
                    
                    
                    // Convert the coordinates in the X and Y dimensions to device
                    // coordinates
                    if (i<2) {
                        value = deviceCoordinates(value,mesh.coords[i][1]);
                    }

                    // Cast other attributes to floating point
                    else if (value.type() != Float(32)) {
                        value = Cast::make(Float(32), value);
                    }
                                                        
                    Value* gpu_coord = codegen(value);
                    builder->CreateStore(gpu_coord,
                                         builder->CreateConstGEP2_32(gpu_coords, 0, c));
                }
                
                builder->CreateStore(gpu_coords,
                                     builder->CreateConstGEP2_64(gpu_coords_per_dim, 0, i));
            }
        }

        // compute a closure over the state passed into the kernel
        GPU_Host_Closure c(loop_stmt, loop->name);
        
        vector<GPU_Argument> closure_args = c.arguments();
        for (size_t i = 0; i < closure_args.size(); i++) {
            if (closure_args[i].is_buffer && allocations.contains(closure_args[i].name)) {
                closure_args[i].size = allocations.get(closure_args[i].name).constant_bytes;
            }
        }

        cgdev->add_kernel(loop_stmt, kernel_name, closure_args);

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
            } else if (ends_with(name, ".varying")) {
                // Expressions for varying attributes are passed in the
                // expression mesh. Pass a non-NULL value in the argument array
                // to keep it in sync with the argument names encoded in the
                // shader header
                val = ConstantInt::get(target_size_t_type, 1);
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
            builder->CreateConstGEP2_32(gpu_args_arr, 0, 0, "gpu_args_arr_ref"),
            gpu_attribute_names,
            gpu_num_attributes,
            gpu_coords_per_dim,
            gpu_num_coords_dim0,
            gpu_num_coords_dim1,
        };

        llvm::Function* dev_run_fn = module->getFunction("halide_dev_run");
        internal_assert(dev_run_fn) << "Could not find halide_dev_run in module\n";
      
        // Make the function extern so that the JIT'ed code will call the debuggable
        // version in the app
        // TODO: Can this operation be specified in the target?
        dev_run_fn->deleteBody();
      
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

#ifdef WITH_PNACL
template class CodeGen_GPU_Host<CodeGen_PNaCl>;
#endif

}}
