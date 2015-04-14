#include "Output.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"

using namespace llvm;

namespace Halide {

namespace {

// Define the mex wrapper API call for the given func with name pipeline_name.
llvm::Function *define_mex_wrapper(const std::string &pipeline_name, llvm::Module *module) {
    LLVMContext &ctx = module->getContext();

    llvm::Function *pipeline = module->getFunction(pipeline_name + "_argv");
    internal_assert(pipeline) << "Did not find function '" << pipeline_name << "_argv' in module.\n";
    llvm::Function *mex_call_pipeline = module->getFunction("halide_mex_call_pipeline");
    internal_assert(mex_call_pipeline) << "Did not find function 'halide_mex_call_pipeline' in module.\n";
    llvm::Value *metadata = module->getGlobalVariable(pipeline_name + "_metadata");
    internal_assert(metadata) << "Did not find global variable '" << pipeline_name << "_metadata' in module.\n";

    llvm::Type *void_ty = llvm::Type::getVoidTy(ctx);
    llvm::Type *i8_ty = llvm::Type::getInt8Ty(ctx);
    llvm::Type *i32_ty = llvm::Type::getInt32Ty(ctx);
    Value *user_context = ConstantPointerNull::get(i8_ty->getPointerTo());

    llvm::Type *mxArray_ty = module->getTypeByName("struct.mxArray");
    internal_assert(mxArray_ty) << "Did not find mxArray in initial module";
    llvm::Type *mxArray_ptr_ty = mxArray_ty->getPointerTo();
    llvm::Type *mxArray_ptr_ptr_ty = mxArray_ptr_ty->getPointerTo();

    // Create the mexFunction function.
    llvm::Type *mex_arg_types[] = {
        i32_ty,
        mxArray_ptr_ptr_ty,
        i32_ty,
        mxArray_ptr_ptr_ty,
    };
    FunctionType *mex_ty = FunctionType::get(void_ty, mex_arg_types, false);
    llvm::Function *mex = llvm::Function::Create(mex_ty, llvm::GlobalValue::ExternalLinkage, "mexFunction", module);
    BasicBlock *entry = BasicBlock::Create(ctx, "entry", mex);

    // Extract the argument values.
    llvm::Function::arg_iterator mex_args = mex->arg_begin();
    Value *nlhs = mex_args++;
    Value *plhs = mex_args++;
    Value *nrhs = mex_args++;
    Value *prhs = mex_args++;

    IRBuilder<> ir(ctx);
    ir.SetInsertPoint(entry);

    Value *call_pipeline_args[] = {
        user_context,
        pipeline,
        metadata,
        nlhs,
        plhs,
        nrhs,
        prhs,
    };
    ir.CreateCall(mex_call_pipeline, call_pipeline_args);
    ir.CreateRetVoid();

    return mex;
}

}  // namespace

void compile_module_to_matlab_object(const Module &module, const std::string &pipeline_name,
                                     const std::string &filename) {
    llvm::LLVMContext context;
    llvm::Module *llvm_module = compile_module_to_llvm_module(module, context);
    define_mex_wrapper(pipeline_name, llvm_module);
    compile_llvm_module_to_object(llvm_module, filename);
    delete llvm_module;
}

}  // namespace Halide
