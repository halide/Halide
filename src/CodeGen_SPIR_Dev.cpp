#include "CodeGen_SPIR_Dev.h"
#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Target.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_SPIR_Dev::CodeGen_SPIR_Dev(Target host, int bits) : CodeGen(host), bits(bits) {
    #if !(WITH_SPIR)
    user_error << "spir not enabled for this build of Halide.\n";
    #endif
}

static vector<Value *> init_kernel_metadata(LLVMContext &ctx, const char *name) {
    vector<Value *> md;
    md.push_back(MDString::get(ctx, name));
    return md;
}

void CodeGen_SPIR_Dev::add_kernel(Stmt stmt, std::string name, const std::vector<Argument> &args) {

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size()+1);
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            arg_types[i] = llvm_type_of(UInt(8))->getPointerTo(1); // __global = addrspace(1)
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }
    // Add local (shared) memory buffer parameter.
    arg_types[args.size()] = llvm_type_of(UInt(8))->getPointerTo(3); // __local = addrspace(3)

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
    // Mark the local memory as no alias (probably not necessary?)
    function->setDoesNotAlias(args.size());


    // Make the initial basic block
    entry_block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry_block);

    vector<Value *> kernel_arg_address_space = init_kernel_metadata(*context, "kernel_arg_addr_space");
    vector<Value *> kernel_arg_access_qual = init_kernel_metadata(*context, "kernel_arg_access_qual");
    vector<Value *> kernel_arg_type = init_kernel_metadata(*context, "kernel_arg_type");
    vector<Value *> kernel_arg_base_type = init_kernel_metadata(*context, "kernel_arg_base_type");
    vector<Value *> kernel_arg_type_qual = init_kernel_metadata(*context, "kernel_arg_type_qual");
    vector<Value *> kernel_arg_name = init_kernel_metadata(*context, "kernel_arg_name");

    // Put the arguments in the symbol table
    {
        llvm::Function::arg_iterator arg = function->arg_begin();
        for (std::vector<Argument>::const_iterator iter = args.begin();
            iter != args.end();
            ++iter, ++arg) {
            if (iter->is_buffer) {
                // HACK: codegen expects a load from foo to use base
                // address 'foo.host', so we store the device pointer
                // as foo.host in this scope.
                sym_push(iter->name + ".host", arg);

                kernel_arg_address_space.push_back(ConstantInt::get(i32, 1));
            } else {
                sym_push(iter->name, arg);

                kernel_arg_address_space.push_back(ConstantInt::get(i32, 0));
            }
            arg->setName(iter->name);

            kernel_arg_name.push_back(MDString::get(*context, iter->name));
            kernel_arg_access_qual.push_back(MDString::get(*context, "none"));
            kernel_arg_type_qual.push_back(MDString::get(*context, ""));
            // TODO: 'Type' isn't correct, but we don't have C to get the type name from...
            // This really shouldn't matter anyways. Everything SPIR needs is in the function
            // type, this metadata seems redundant.
            kernel_arg_type.push_back(MDString::get(*context, "type"));
            kernel_arg_base_type.push_back(MDString::get(*context, "type"));
        }
        arg->setName("shared");
        shared_mem = arg;

        kernel_arg_address_space.push_back(ConstantInt::get(i32, 3)); // __local = addrspace(3)
        kernel_arg_name.push_back(MDString::get(*context, "shared"));
        kernel_arg_access_qual.push_back(MDString::get(*context, "none"));
        kernel_arg_type.push_back(MDString::get(*context, "char*"));
        kernel_arg_base_type.push_back(MDString::get(*context, "char*"));
        kernel_arg_type_qual.push_back(MDString::get(*context, ""));
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
    Value *kernel_metadata[] =
    {
        function,
        MDNode::get(*context, kernel_arg_address_space),
        MDNode::get(*context, kernel_arg_access_qual),
        MDNode::get(*context, kernel_arg_type),
        MDNode::get(*context, kernel_arg_type_qual),
        MDNode::get(*context, kernel_arg_name)
    };
    MDNode *mdNode = MDNode::get(*context, kernel_metadata);
    module->getOrInsertNamedMetadata("opencl.kernels")->addOperand(mdNode);


    // Now verify the function is ok
    verifyFunction(*function);

    // Finally, verify the module is ok
    verifyModule(*module);
    debug(2) << "Done generating llvm bitcode\n";
}

void CodeGen_SPIR_Dev::init_module() {

    CodeGen::init_module();

    module = get_initial_module_for_spir_device(context, bits);

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
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_SPIR_Dev::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        debug(2) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        internal_assert(loop->for_type == For::Parallel) << "kernel loop must be parallel\n";

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
        //ptr = builder->CreateIntToPtr(offset, PointerType::get(llvm_type, 3));
        if (bits == 64) {
            llvm::Type *i64 = llvm::Type::getInt64Ty(*context);
            offset = builder->CreateIntCast(offset, i64, false);
        }
        ptr = builder->CreateInBoundsGEP(shared_mem, offset);
        ptr = builder->CreatePointerCast(ptr, PointerType::get(llvm_type, 3));
    } else {
        // Otherwise jump back to the entry and generate an
        // alloca. Note that by jumping back we're rendering any
        // expression we carry back meaningless, so we had better only
        // be dealing with constants here.
        int32_t size = 0;
        bool is_constant = constant_allocation_size(alloc->extents, allocation_name, size);
        user_assert(is_constant)
            << "Allocation " << alloc->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. Try storing into shared memory instead.\n";

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32, size));
        builder->SetInsertPoint(here);
    }

    sym_push(allocation_name, ptr);
    codegen(alloc->body);
}

void CodeGen_SPIR_Dev::visit(const Free *f) {
    sym_pop(f->name + ".host");
}

string CodeGen_SPIR_Dev::march() const {
    return bits == 32 ? "spir" : "spir64";
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

    optimize_module();

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
