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

            const StringImm *string_imm = op->args[0].as<StringImm>();
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
    cgdev(make_devices(target)) {
}

template<typename CodeGen_CPU>
std::map<DeviceAPI, CodeGen_GPU_Dev *> CodeGen_GPU_Host<CodeGen_CPU>::make_devices(Target t)
{
    std::map<DeviceAPI, CodeGen_GPU_Dev *> result;

    // For the default GPU, OpenCL is preferred, CUDA next, and OpenGL last.
    // The code is in reverse order to allow later tests to override earlier ones.
    DeviceAPI default_api = DeviceAPI::Default_GPU;
    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Constructing OpenGL device codegen\n";
        result[DeviceAPI::GLSL] = new CodeGen_OpenGL_Dev(t);
        default_api = DeviceAPI::GLSL;
    }
    if (t.has_feature(Target::CUDA)) {
        debug(1) << "Constructing CUDA device codegen\n";
        result[DeviceAPI::CUDA] = new CodeGen_PTX_Dev(t);
        default_api = DeviceAPI::CUDA;
    }
    if (t.has_feature(Target::OpenCL)) {
        debug(1) << "Constructing OpenCL device codegen\n";
        result[DeviceAPI::OpenCL] = new CodeGen_OpenCL_Dev(t);
        default_api = DeviceAPI::OpenCL;
    }

    if (result.empty()) {
        internal_error << "Requested unknown GPU target: " << t.to_string() << "\n";
    } else {
        result[DeviceAPI::Default_GPU] = result[default_api];
    }
    return result;
}

template<typename CodeGen_CPU>
CodeGen_GPU_Host<CodeGen_CPU>::~CodeGen_GPU_Host() {
    std::map<DeviceAPI, CodeGen_GPU_Dev *>::iterator iter;
    for (iter = cgdev.begin(); iter != cgdev.end(); iter++) {
        if (iter->first != DeviceAPI::Default_GPU) {
            delete iter->second;
        }
    }
}

template<typename CodeGen_CPU>
void CodeGen_GPU_Host<CodeGen_CPU>::compile(Stmt stmt, string name,
                                            const vector<Argument> &args,
                                            const vector<Buffer> &images_to_embed) {

    init_module();

    // also set up the child codegenerator - this is set up once per
    // PTX_Host::compile, and reused across multiple PTX_Dev::compile
    // invocations for different kernels.
    std::map<DeviceAPI, CodeGen_GPU_Dev *>::iterator iter;
    for (iter = cgdev.begin(); iter != cgdev.end(); iter++) {
        iter->second->init_module();
    }

    module = get_initial_module_for_target(target, context);

    if (target.has_feature(Target::JIT)) {
        std::vector<JITModule> shared_runtime = JITSharedRuntime::get(this, target);

        JITModule::make_externs(shared_runtime, module);
    }

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


    // Remember the entry block so we can branch to it upon init success.
    BasicBlock *entry = &function->getEntryBlock();

    for (iter = cgdev.begin(); iter != cgdev.end(); iter++) {
        if (iter->first == DeviceAPI::Default_GPU) {
            continue;
        }
        CodeGen_GPU_Dev *gpu_codegen = iter->second;
        std::string api_unique_name = gpu_codegen->api_unique_name();
        debug(2) << "Generating init_kernels for " << api_unique_name << "\n";

        std::vector<char> kernel_src = gpu_codegen->compile_to_src();

        Value *kernel_src_ptr = CodeGen_CPU::create_constant_binary_blob(kernel_src, "halide_" + api_unique_name + "_kernel_src");

        // Insert a new block to run initialization at the beginning of the function.
        BasicBlock *init_kernels_bb = BasicBlock::Create(*context, "init_kernels" + api_unique_name,
                                                         function, entry);
        builder->SetInsertPoint(init_kernels_bb);
        Value *user_context = get_user_context();
        Value *kernel_size = ConstantInt::get(i32, kernel_src.size());
        std::string init_kernels_name = "halide_" + api_unique_name + "_initialize_kernels";
        Value *init = module->getFunction(init_kernels_name);
        internal_assert(init) << "Could not find function " + init_kernels_name + " in initial module\n";
        Value *result = builder->CreateCall4(init, user_context,
                                             get_module_state(api_unique_name),
                                             kernel_src_ptr, kernel_size);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside " + init_kernels_name);

        // Upon success, jump to the previous entry.
        builder->CreateBr(entry);
        // Set the entry for the next init block to the init block just generated
        entry = init_kernels_bb;
    }

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
        
        Value *null_float_ptr = ConstantPointerNull::get(CodeGen::f32->getPointerTo());
        Value *zero_int32 = codegen(Expr(cast<int>(0)));

        Value *gpu_num_padded_attributes  = zero_int32;
        Value *gpu_vertex_buffer   = null_float_ptr;
        Value *gpu_num_coords_dim0 = zero_int32;
        Value *gpu_num_coords_dim1 = zero_int32;

        if (loop->device_api == DeviceAPI::GLSL) {
            
            // GL draw calls that invoke the GLSL shader are issued for pairs of
            // for-loops over spatial x and y dimensions. For each for-loop we create
            // one scalar vertex attribute for the spatial dimension corresponding to
            // that loop, plus one scalar attribute for each expression previously
            // labeled as "glsl_varying"

            // Pass variables created during setup_gpu_vertex_buffer to the
            // dev run function call.
            gpu_num_padded_attributes = codegen(Variable::make(Int(32), "glsl.num_padded_attributes"));
            gpu_num_coords_dim0 = codegen(Variable::make(Int(32), "glsl.num_coords_dim0"));
            gpu_num_coords_dim1 = codegen(Variable::make(Int(32), "glsl.num_coords_dim1"));

            // Look up the allocation for the vertex buffer and cast it to the
            // right type
            gpu_vertex_buffer = codegen(Variable::make(Handle(), "glsl.vertex_buffer.host"));
            gpu_vertex_buffer = builder->CreatePointerCast(gpu_vertex_buffer,
                                                           CodeGen::f32->getPointerTo());

        }

        // compute a closure over the state passed into the kernel
        GPU_Host_Closure c(loop, loop->name);
        
        // Determine the arguments that must be passed into the halide function
        vector<GPU_Argument> closure_args = c.arguments();

        // Halide allows passing of scalar float and integer arguments. For
        // OpenGL, pack these into vec4 uniforms and varying attributes
        if (target.has_feature(Target::OpenGL)) {

            int num_uniform_floats = 0;

            // The spatial x and y coordinates are passed in the first two
            // scalar float varying slots
            int num_varying_floats = 2;
            int num_uniform_ints   = 0;

            // Pack scalar parameters into vec4
            for (size_t i = 0; i < closure_args.size(); i++) {
                if (closure_args[i].is_buffer) {
                    continue;
                } else if (ends_with(closure_args[i].name, ".varying")) {
                    closure_args[i].packed_index = num_varying_floats++;
                } else if (closure_args[i].type.is_float()) {
                    closure_args[i].packed_index = num_uniform_floats++;
                } else if (closure_args[i].type.is_int()) {
                    closure_args[i].packed_index = num_uniform_ints++;
                }
            }
        }

        for (size_t i = 0; i < closure_args.size(); i++) {
            if (closure_args[i].is_buffer && allocations.contains(closure_args[i].name)) {
                closure_args[i].size = allocations.get(closure_args[i].name).constant_bytes;
            }
        }

        CodeGen_GPU_Dev *gpu_codegen = cgdev[loop->device_api];
        gpu_codegen->add_kernel(loop, kernel_name, closure_args);

        // get the actual name of the generated kernel for this loop
        kernel_name = gpu_codegen->get_current_kernel_name();
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

        Value *gpu_arg_is_buffer_arr =
            create_alloca_at_entry(ArrayType::get(i8, num_args+1),
                                   num_args+1,
                                   kernel_name + "_arg_is_buffer");

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

            // store a void * pointer to the argument into the gpu_args_arr
            Value *bits = builder->CreateBitCast(ptr, arg_t);
            builder->CreateStore(bits,
                                 builder->CreateConstGEP2_32(gpu_args_arr, 0, i));

            // store the size of the argument
            int size_bits = (closure_args[i].is_buffer) ? target.bits : closure_args[i].type.bits;
            builder->CreateStore(ConstantInt::get(target_size_t_type, size_bits/8),
                                 builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, i));

            builder->CreateStore(ConstantInt::get(i8, closure_args[i].is_buffer),
                                 builder->CreateConstGEP2_32(gpu_arg_is_buffer_arr, 0, i));
        }
        // NULL-terminate the lists
        builder->CreateStore(ConstantPointerNull::get(arg_t),
                             builder->CreateConstGEP2_32(gpu_args_arr, 0, num_args));
        builder->CreateStore(ConstantInt::get(target_size_t_type, 0),
                             builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, num_args));
        builder->CreateStore(ConstantInt::get(i8, 0),
                             builder->CreateConstGEP2_32(gpu_arg_is_buffer_arr, 0, num_args));

        std::string api_unique_name = gpu_codegen->api_unique_name();

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid[3]?
        internal_assert(is_one(bounds.num_threads[3]) && is_one(bounds.num_blocks[3]));
        Value *launch_args[] = {
            get_user_context(),
            builder->CreateLoad(get_module_state(api_unique_name)),
            entry_name_str,
            codegen(bounds.num_blocks[0]), codegen(bounds.num_blocks[1]), codegen(bounds.num_blocks[2]),
            codegen(bounds.num_threads[0]), codegen(bounds.num_threads[1]), codegen(bounds.num_threads[2]),
            codegen(bounds.shared_mem_size),
            builder->CreateConstGEP2_32(gpu_arg_sizes_arr, 0, 0, "gpu_arg_sizes_ar_ref" + api_unique_name),
            builder->CreateConstGEP2_32(gpu_args_arr, 0, 0, "gpu_args_arr_ref" + api_unique_name),
            builder->CreateConstGEP2_32(gpu_arg_is_buffer_arr, 0, 0, "gpu_arg_is_buffer_ref" + api_unique_name),
            gpu_num_padded_attributes,
            gpu_vertex_buffer,
            gpu_num_coords_dim0,
            gpu_num_coords_dim1,
        };
        std::string run_fn_name = "halide_" + api_unique_name + "_run";
        llvm::Function *dev_run_fn = module->getFunction(run_fn_name);
        internal_assert(dev_run_fn) << "Could not find " << run_fn_name << " in module\n";
        Value *result = builder->CreateCall(dev_run_fn, launch_args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        CodeGen_CPU::create_assertion(did_succeed, "Failure inside " + run_fn_name);
    } else {
        CodeGen_CPU::visit(loop);
    }
}

template<typename CodeGen_CPU>
Value *CodeGen_GPU_Host<CodeGen_CPU>::get_module_state(const std::string &api_unique_name) {
    GlobalVariable *module_state = module->getGlobalVariable("module_state" + api_unique_name, true);
    if (!module_state)
    {
        // Create a global variable to hold the module state
        PointerType *void_ptr_type = llvm::Type::getInt8PtrTy(*context);
        module_state = new GlobalVariable(*module, void_ptr_type,
                                          false, GlobalVariable::PrivateLinkage,
                                          ConstantPointerNull::get(void_ptr_type),
                                          "module_state" + api_unique_name);
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
