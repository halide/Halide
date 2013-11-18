#include "CodeGen_SPIR_Dev.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Debug.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"
#include "Target.h"
#include "integer_division_table.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_SPIR_Dev::CodeGen_SPIR_Dev() : CodeGen() {
    #if !(WITH_SPIR)
    assert(false && "spir not enabled for this build of Halide.");
    #endif
}

void CodeGen_SPIR_Dev::compile(Stmt stmt, std::string name, const std::vector<Argument> &args) {

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            arg_types[i] = llvm_type_of(UInt(8))->getPointerTo(1);
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }

    // Make our function
    function_name = name;
    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module);
    function->setCallingConv(llvm::CallingConv::SPIR_KERNEL);

    // Mark the buffer args as no alias
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            function->setDoesNotAlias(i+1);
        }
    }


    // Make the initial basic block
    entry_block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry_block);

    // Put the arguments in the symbol table
    {
        size_t i = 0;
        for (llvm::Function::arg_iterator iter = function->arg_begin();
             iter != function->arg_end();
             iter++) {

            if (args[i].is_buffer) {
                // HACK: codegen expects a load from foo to use base
                // address 'foo.host', so we store the device pointer
                // as foo.host in this scope.
                sym_push(args[i].name + ".host", iter);
            } else {
                sym_push(args[i].name, iter);
            }
            iter->setName(args[i].name);

            i++;
        }
    }

    // We won't end the entry block yet, because we'll want to add
    // some allocas to it later if there are local allocations. Start
    // a new block to put all the code.
    BasicBlock *body_block = BasicBlock::Create(*context, "body", function);
    builder->SetInsertPoint(body_block);

    debug(1) << "Generating llvm bitcode...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRetVoid();

    // Make the entry block point to the body block
    builder->SetInsertPoint(entry_block);
    builder->CreateBr(body_block);

    // Add the nvvm annotation that it is a kernel function.
    MDNode *mdNode = MDNode::get(*context, vec<Value *>(function));
    module->getOrInsertNamedMetadata("opencl.kernels")->addOperand(mdNode);


    // Now verify the function is ok
    verifyFunction(*function);

    // Finally, verify the module is ok
    verifyModule(*module);
    debug(2) << "Done generating llvm bitcode\n";
}

const std::string spirDataLayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024";
const std::string spir64DataLayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024";

void CodeGen_SPIR_Dev::init_module() {

    CodeGen::init_module();

    module = get_initial_module_for_spir_device(context);

    module->setTargetTriple(Triple::normalize(march()+"-unknown-unknown"));
    module->setDataLayout(spirDataLayout);

    // Fix the target triple
    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    module->setModuleIdentifier("<halide_spir>");
    
    for(Module::iterator i = module->begin(); i != module->end(); ++i)
        i->setCallingConv(CallingConv::SPIR_FUNC);

    owns_module = true;
}

string CodeGen_SPIR_Dev::simt_intrinsic(const string &name) {
    if (ends_with(name, ".threadidx")) {
        return "halide.spir.lid.x";
    } else if (ends_with(name, ".threadidy")) {
        return "halide.spir.lid.y";
    } else if (ends_with(name, ".threadidz")) {
        return "halide.spir.lid.z";
    } 
    //else if (ends_with(name, ".threadidw")) {
    //    return "halide.spir.lid.w";
    //} 
    else if (ends_with(name, ".blockidx")) {
        return "halide.spir.gid.x";
    } else if (ends_with(name, ".blockidy")) {
        return "halide.spir.gid.y";
    } else if (ends_with(name, ".blockidz")) {
        return "halide.spir.gid.z";
    } 
    //else if (ends_with(name, ".blockidw")) {
    //    return "halide.spir.gid.w";
    //}
    assert(false && "simt_intrinsic called on bad variable name");
}

void CodeGen_SPIR_Dev::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        debug(2) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Parallel && "kernel loop must be parallel");

        Expr simt_idx = Call::make(Int(32), simt_intrinsic(loop->name), std::vector<Expr>(), Call::Extern);
        Expr loop_var = loop->min + simt_idx;
        Expr cond = simt_idx < loop->extent;
        debug(3) << "for -> if (" << cond << ")\n";

        BasicBlock *loop_bb = BasicBlock::Create(*context, loop->name + "_loop", function);
        BasicBlock *after_bb = BasicBlock::Create(*context, loop->name + "_after_loop", function);

        builder->CreateCondBr(codegen(cond), loop_bb, after_bb);
        builder->SetInsertPoint(loop_bb);

        sym_push(loop->name, codegen(loop_var));
        codegen(loop->body);
        sym_pop(loop->name);

        builder->CreateBr(after_bb);
        builder->SetInsertPoint(after_bb);
    } else {
        CodeGen::visit(loop);
    }
}

void CodeGen_SPIR_Dev::visit(const Pipeline *n) {
    n->produce.accept(this);

    // Grab the syncthreads intrinsic, or declare it if it doesn't exist yet
    llvm::Function *barrier = module->getFunction("halide.spir.barrier");

    if (n->update.defined()) {
        // If we're producing into shared or global memory we need a
        // syncthreads before continuing.
        builder->CreateCall(barrier, std::vector<Value *>());
        n->update.accept(this);
    }

    builder->CreateCall(barrier, std::vector<Value *>());
    n->consume.accept(this);
}

void CodeGen_SPIR_Dev::visit(const Allocate *alloc) {

    debug(1) << "Allocate " << alloc->name << " on device\n";

    llvm::Type *llvm_type = llvm_type_of(alloc->type);

    string allocation_name = alloc->name + ".host";
    debug(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

    // If this is a shared allocation, there should already be a
    // pointer into shared memory in the symbol table.
    Value *ptr;
    Value *offset = sym_get(alloc->name + ".shared_mem", false);

    if (offset) {
        // Bit-cast it to a shared memory pointer (address-space 3 is shared memory)
        ptr = builder->CreateIntToPtr(offset, PointerType::get(llvm_type, 3));
    } else {
        // Otherwise jump back to the entry and generate an
        // alloca. Note that by jumping back we're rendering any
        // expression we carry back meaningless, so we had better only
        // be dealing with constants here.
        const IntImm *size = alloc->size.as<IntImm>();
        assert(size && "Only fixed-size allocations are supported on the gpu. Try storing into shared memory instead.");

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32, size->value));
        builder->SetInsertPoint(here);
    }

    sym_push(allocation_name, ptr);
    codegen(alloc->body);
    
    // Optimize it - this really only optimizes the current function
    optimize_module();
}

void CodeGen_SPIR_Dev::visit(const Free *f) {
    sym_pop(f->name + ".host");
}

string CodeGen_SPIR_Dev::march() const {
    return "spir";
}

string CodeGen_SPIR_Dev::mcpu() const {
    return "unknown";
}

string CodeGen_SPIR_Dev::mattrs() const {
    return "unknown";
}

bool CodeGen_SPIR_Dev::use_soft_float_abi() const {
    return false;
}

vector<char> CodeGen_SPIR_Dev::compile_to_src() {
    
    SmallVector<char, 1024> buffer;
    raw_svector_ostream stream(buffer);
    WriteBitcodeToFile(module, stream);
    stream.flush();
    return vector<char>(buffer.begin(), buffer.end());
}


string CodeGen_SPIR_Dev::get_current_kernel_name() {
    return function->getName();
}

void CodeGen_SPIR_Dev::dump() {
    module->dump();
}

}}
