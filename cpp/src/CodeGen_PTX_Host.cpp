#include "CodeGen_PTX_Host.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Log.h"
#include "Var.h"
#include "Param.h"
#include "integer_division_table.h"
#include "CodeGen_Internal.h"
#include "Util.h"
#include "Bounds.h"
#include "Simplify.h"

#include <dlfcn.h>
#include <unistd.h>

extern "C" unsigned char halide_internal_initmod_ptx_host[];
extern "C" int halide_internal_initmod_ptx_host_length;

namespace Halide { 
namespace Internal {

extern "C" { typedef struct CUctx_st *CUcontext; }
CUcontext cuda_ctx = 0;

using std::vector;
using std::string;
using std::map;

using namespace llvm;

bool CodeGen_PTX_Host::lib_cuda_linked = false;

class CodeGen_PTX_Host::Closure : public Halide::Internal::Closure {
public:
    static Closure make(Stmt s, const std::string &lv, bool skip_gpu_loops=false) {
        Closure c;
        c.skip_gpu_loops = skip_gpu_loops;
        c.ignore.push(lv, 0);
        s.accept(&c);
        return c;
    }

    vector<Argument> arguments();

protected:
    using Internal::Closure::visit;

    void visit(const For *op);

    bool skip_gpu_loops;
};

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
        return max(a, b);
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
        assert(table.find(allocate->name) == table.end());

        // What's the largest this allocation could be (in bytes)?
        Expr elements = bounds_of_expr_in_scope(allocate->size, scope).max;
        int bytes_per_element = allocate->type.bits/8;
        table[allocate->name] = simplify(elements * bytes_per_element);

        allocate->body.accept(this);

    }
};

vector<Argument> CodeGen_PTX_Host::Closure::arguments() {
    vector<Argument> res;
    map<string, Type>::const_iterator iter;
    for (iter = vars.begin(); iter != vars.end(); ++iter) {
        log(2) << "var: " << iter->first << "\n";
        res.push_back(Argument(iter->first, false, iter->second));
    }
    for (iter = reads.begin(); iter != reads.end(); ++iter) {
        log(2) << "read: " << iter->first << "\n";
        res.push_back(Argument(iter->first, true, iter->second));
    }
    for (iter = writes.begin(); iter != writes.end(); ++iter) {
        log(2) << "write: " << iter->first << "\n";
        res.push_back(Argument(iter->first, true, iter->second));
    }
    return res;
}


void CodeGen_PTX_Host::Closure::visit(const For *loop) {
    if (skip_gpu_loops && 
        CodeGen_PTX_Dev::is_simt_var(loop->name)) {
        return;
    }
    Internal::Closure::visit(loop);
}


CodeGen_PTX_Host::CodeGen_PTX_Host(uint32_t options) :
    CodeGen_X86(options) {
}

void CodeGen_PTX_Host::compile(Stmt stmt, string name, const vector<Argument> &args) {
    
    init_module();

    // also set up the child codegenerator - this is set up once per
    // PTX_Host::compile, and reused across multiple PTX_Dev::compile
    // invocations for different kernels.
    cgdev.init_module();

    StringRef sb;

    assert(use_64_bit && !use_avx && "PTX Host only built for simple x86_64 for now");
    sb = StringRef((char *)halide_internal_initmod_ptx_host,
                           halide_internal_initmod_ptx_host_length);
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it
    std::string errstr;
    module = ParseBitcodeFile(bitcode_buffer, *context, &errstr);
    if (!module) {
        std::cerr << "Error parsing initial module: " << errstr << "\n";
    }
    assert(module && "llvm encountered an error in parsing a bitcode file.");

    // grab runtime helper functions
    dev_malloc_if_missing_fn = module->getFunction("halide_dev_malloc_if_missing");
    assert(dev_malloc_if_missing_fn && "Could not find halide_dev_malloc_if_missing in module");

    copy_to_host_fn = module->getFunction("halide_copy_to_host");
    assert(copy_to_host_fn && "Could not find halide_copy_to_host in module");

    copy_to_dev_fn = module->getFunction("halide_copy_to_dev");
    assert(copy_to_dev_fn && "Could not find halide_copy_to_dev in module");

    dev_run_fn = module->getFunction("halide_dev_run");
    assert(dev_run_fn && "Could not find halide_dev_run in module");

    // Fix the target triple
    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    // For now we'll just leave it as whatever the module was
    // compiled as. This assumes that we're not cross-compiling
    // between different x86 operating systems
    // module->setTargetTriple( ... );

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args);

    if (log::debug_level >= 2) {
        cgdev.module->dump();
        module->dump();
    }

    string ptx_src = cgdev.compile_to_ptx();
    log(2) << ptx_src;
    llvm::Type *ptx_src_type = ArrayType::get(i8, ptx_src.size()+1);
    GlobalVariable *ptx_src_global = new GlobalVariable(*module, ptx_src_type, 
                                                         true, GlobalValue::PrivateLinkage, 0,
                                                         "halide_ptx_src");
    ptx_src_global->setInitializer(ConstantDataArray::getString(*context, ptx_src));

    builder->SetInsertPoint(function->getEntryBlock().getFirstInsertionPt());
    Value *ptx_src_ptr = builder->CreateConstInBoundsGEP2_32(ptx_src_global, 0, 0);
    Value *init = module->getFunction("halide_init_kernels");
    builder->CreateCall(init, ptx_src_ptr);

    delete bitcode_buffer;
}

void CodeGen_PTX_Host::jit_init(ExecutionEngine *ee, Module *module) {

    // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
    // CUDA behaves much better when you don't initialize >2 contexts.
    GlobalValue *cu_ctx = module->getNamedGlobal("cuda_ctx");
    if (cu_ctx) {
        ee->addGlobalMapping(cu_ctx, (void*)&cuda_ctx);
    }

    // Make sure extern cuda calls inside the module point to the
    // right things. If cuda is already linked in we should be
    // fine. If not we need to tell llvm to load it.
    if (!lib_cuda_linked) {
        // First check if libCuda has already been linked
        // in. If so we shouldn't need to set any mappings.
        if (dlsym(NULL, "cuInit")) {
            log(1) << "This program was linked to cuda already\n";
        } else {
            log(1) << "Looking for cuda shared library...\n";
            string error;
            llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.so", &error);
            if (!error.empty()) {
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.dylib", &error);
            }
            if (!error.empty()) {
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("nvcuda.dll", &error);
            }
            assert(error.empty() && "Could not find libcuda.so, libcuda.dylib, or nvcuda.dll");
        }
        lib_cuda_linked = true;
    }
    
}

void CodeGen_PTX_Host::visit(const For *loop) {
    if (CodeGen_PTX_Dev::is_simt_var(loop->name)) {
        log(2) << "Kernel launch: " << loop->name << "\n";

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
        log(2) << "Kernel bounds: ("
               << n_tid_x << ", " << n_tid_y << ", " << n_tid_z << ", " << n_tid_w << ") threads, ("
               << n_blkid_x << ", " << n_blkid_y << ", " << n_blkid_z << ", " << n_blkid_w << ") blocks\n";

        // compute a closure over the state passed into the kernel
        Closure c = Closure::make(loop, loop->name);

        // Note that we currently do nothing with the thread-local
        // allocations found. Currently we only handle const-sized
        // ones, and we handle those by generating allocas at the top
        // of the device kernel.

        // Compute and pass in offsets into shared memory for all the internal allocations
        Expr n_threads = n_tid_x * n_tid_y * n_tid_z * n_tid_w;
        Value *shared_mem_size = ConstantInt::get(i32, 0);
        vector<string> shared_mem_allocations;
        for (map<string, Expr>::iterator iter = bounds.shared_allocations.begin(); 
             iter != bounds.shared_allocations.end(); iter++) {

            log(2) << "Internal shared allocation" << iter->first 
                   << " has max size " << iter->second << "\n";
            
            Value *size = codegen(iter->second);

            string name = iter->first + ".shared_mem";
            shared_mem_allocations.push_back(name);
            sym_push(name, shared_mem_size);
            c.vars[name] = Int(32);
            
            shared_mem_size = builder->CreateAdd(shared_mem_size, size);
        }        

        // compile the kernel
        string kernel_name = "kernel_" + loop->name;
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (kernel_name[i] == '.') kernel_name[i] = '_';
        }
        cgdev.compile(loop, kernel_name, c.arguments());

        // set up the buffer arguments for the device
        map<string, Type>::iterator it;
        for (it = c.reads.begin(); it != c.reads.end(); ++it) {
            log(4) << "halide_dev_malloc_if_missing " << it->first << " (read)\n";
            log(4) << "halide_copy_to_dev " << it->first << "\n";
            Value *buf = sym_get(it->first + ".buffer");
            builder->CreateCall(dev_malloc_if_missing_fn, buf);
            builder->CreateCall(copy_to_dev_fn, buf);
        }
        for (it = c.writes.begin(); it != c.writes.end(); ++it) {
            log(4) << "halide_dev_malloc_if_missing " << it->first << " (write)\n";
            Value *buf = sym_get(it->first + ".buffer");
            builder->CreateCall(dev_malloc_if_missing_fn, buf);
        }

        // get the actual name of the generated kernel for this loop
        kernel_name = cgdev.function->getName();
        log(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";
        Value *entry_name_str = builder->CreateGlobalStringPtr(kernel_name, "entry_name");

        // build the kernel arguments array
        vector<Argument> closure_args = c.arguments();
        llvm::Type *arg_t = i8->getPointerTo(); // void*
        int num_args = (int)closure_args.size();
        // TODO: save the stack?
        Value *gpu_args_arr = builder->CreateAlloca(ArrayType::get(arg_t, num_args+1), // NULL-terminated list
                                                    NULL,
                                                    kernel_name + "_args");

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

            // allocate stack space to mirror the closure element
            // TODO: this should be unnecessary!
            // TODO: save the stack?
            Value *ptr = builder->CreateAlloca(val->getType(), NULL, name+".stack");
            // store the closure value into the stack space
            builder->CreateStore(val, ptr);

            // store a void* pointer to the argument into the gpu_args_arr
            Value *bits = builder->CreateBitCast(ptr, arg_t);
            builder->CreateStore(bits,
                                 builder->CreateConstGEP2_32(gpu_args_arr, 0, i));
        }

        // Figure out how much shared memory we need to allocate, and
        // build offsets into it for the internal allocations

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid_w?
        Value *launch_args[] = {
            entry_name_str,
            codegen(n_blkid_x), codegen(n_blkid_y), codegen(n_blkid_z),
            codegen(n_tid_x), codegen(n_tid_y), codegen(n_tid_z),
            shared_mem_size, 
            builder->CreateConstGEP2_32(gpu_args_arr, 0, 0, "gpu_args_arr_ref")
        };
        builder->CreateCall(dev_run_fn, launch_args);

        // mark written buffers dirty
        for (it = c.writes.begin(); it != c.writes.end(); ++it) {
            log(4) << "setting dev_dirty " << it->first << " (write)\n";
            Value *buf = sym_get(it->first + ".buffer");
            builder->CreateStore(ConstantInt::get(i8, 1), buffer_dev_dirty_ptr(buf));
        }

        // Pop the shared memory allocations from the symbol table
        for (size_t i = 0; i < shared_mem_allocations.size(); i++) {
            sym_pop(shared_mem_allocations[i]);
        }
    } else {
        CodeGen_X86::visit(loop);
    }
}

void CodeGen_PTX_Host::visit(const Allocate *alloc) {
    Value *saved_stack;
    Value *ptr = malloc_buffer(alloc, saved_stack);

    // create a buffer_t to track this allocation
    // TODO: we need to reset the stack pointer, regardless of whether the
    //       main allocation was on the stack or heap.
    Value *buf = builder->CreateAlloca(buffer_t);
    Value *zero32 = ConstantInt::get(i32, 0, "zero"),
          *one32  = ConstantInt::get(i32, 1, "one"),
          *null64 = ConstantInt::get(i64, 0, "null"),
          *zero8  = ConstantInt::get( i8, 0, "zero");

    Value *host_ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
    builder->CreateStore(host_ptr, buffer_host_ptr(buf), "set_host");
    builder->CreateStore(null64,   buffer_dev_ptr(buf), "set_dev");

    builder->CreateStore(zero8,  buffer_host_dirty_ptr(buf), "set_host_dirty");
    builder->CreateStore(zero8,  buffer_dev_dirty_ptr(buf), "set_dev_dirty");

    // For now, we just track the allocation as a single 1D buffer of the
    // required size. If we break this into multiple dimensions, this will
    // fail to account for expansion for alignment.
    builder->CreateStore(codegen(alloc->size),
                                buffer_extent_ptr(buf, 0));
    builder->CreateStore(one32,   buffer_extent_ptr(buf, 1));
    builder->CreateStore(one32,   buffer_extent_ptr(buf, 2));
    builder->CreateStore(one32,   buffer_extent_ptr(buf, 3));

    builder->CreateStore(one32,   buffer_stride_ptr(buf, 0));
    builder->CreateStore(one32,   buffer_stride_ptr(buf, 1));
    builder->CreateStore(one32,   buffer_stride_ptr(buf, 2));
    builder->CreateStore(one32,   buffer_stride_ptr(buf, 3));

    builder->CreateStore(zero32,  buffer_min_ptr(buf, 0));
    builder->CreateStore(zero32,  buffer_min_ptr(buf, 1));
    builder->CreateStore(zero32,  buffer_min_ptr(buf, 2));
    builder->CreateStore(zero32,  buffer_min_ptr(buf, 3));

    int bytes = alloc->type.width * alloc->type.bits / 8;
    builder->CreateStore(ConstantInt::get(i32, bytes),
                                buffer_elem_size_ptr(buf));

    log(3) << "Pushing allocation called " << alloc->name << " onto the symbol table\n";

    sym_push(alloc->name + ".buffer", buf);
    sym_push(alloc->name + ".host", ptr);
    codegen(alloc->body);
    sym_pop(alloc->name + ".host");
    sym_pop(alloc->name + ".buffer");

    // Call halide_free_dev_buffer to free device memory, if needed
    llvm::Function *free_buf_fn = module->getFunction("halide_free_dev_buffer");
    assert(free_buf_fn && "Could not find halide_free_dev_buffer in module");
    log(4) << "Creating call to halide_free_dev_buffer\n";
    builder->CreateCall(free_buf_fn, buf);

    // free the underlying host buffer
    // TODO: at some point, we should also be able to lazily allocate
    //       intermediate *host* memory, in case it never gets used outside
    //       the device.
    free_buffer(ptr, saved_stack);
}

void CodeGen_PTX_Host::visit(const Pipeline *n) {
    Value *buf = sym_get(n->name + ".buffer");

    Closure produce = Closure::make(n->produce, "", true);
    Closure consume = Closure::make(n->consume, "", true);

    codegen(n->produce);

    // Track host writes
    if (produce.writes.count(n->name)) {
        builder->CreateStore(ConstantInt::get(i8, 1),
                             buffer_host_dirty_ptr(buf));
    }

    if (n->update.defined()) {
        Closure update = Closure::make(n->update, "", true);

        // Copy back host update reads
        if (update.reads.count(n->name)) {
            builder->CreateCall(copy_to_host_fn, buf);
        }

        codegen(n->update);

        // Track host update writes
        if (update.writes.count(n->name)) {
            builder->CreateStore(ConstantInt::get(i8, 1),
                                 buffer_host_dirty_ptr(buf));
        }
    }

    // Copy back host reads
    if (consume.reads.count(n->name)) {
        builder->CreateCall(copy_to_host_fn, buf);
    }

    codegen(n->consume);
}

void CodeGen_PTX_Host::test() {
    Argument buffer_arg("buf", true, Int(0));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    vector<Argument> args(3);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;
    Var bx("blockidx");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = new Select(alpha > 4.0f, 3, 2);
    Stmt s = new Store("buf", e, bx);
    s = new For(bx.name(), 0, 16, For::Parallel, s);

    CodeGen_PTX_Host cg(X86_64Bit | X86_SSE41);
    cg.compile(s, "test1", args);
}

}}
