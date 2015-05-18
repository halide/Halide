#include "Error.h"
#include "LLVM_Headers.h"

using namespace llvm;

namespace Halide {
namespace Internal {

// Define the mex wrapper API call (mexFunction) for a func with name pipeline_name.
llvm::Function *define_matlab_wrapper(llvm::Module *module, const std::string &pipeline_name) {
    user_assert(!module->getFunction("mexFunction"))
        << "Module already contains a mexFunction. Only one pipeline can define a mexFunction.\n";

    LLVMContext &ctx = module->getContext();

    llvm::Function *pipeline = module->getFunction(pipeline_name + "_argv");
    internal_assert(pipeline) << "Did not find function '" << pipeline_name << "_argv' in module.\n";
    llvm::Value *metadata = module->getGlobalVariable(pipeline_name + "_metadata");
    internal_assert(metadata) << "Did not find global variable '" << pipeline_name << "_metadata' in module.\n";
    llvm::Function *call_pipeline = module->getFunction("halide_matlab_call_pipeline");
    internal_assert(call_pipeline) << "Did not find function 'halide_matlab_call_pipeline' in module.\n";

    llvm::Type *void_ty = llvm::Type::getVoidTy(ctx);
    llvm::Type *i8_ty = llvm::Type::getInt8Ty(ctx);
    llvm::Type *i32_ty = llvm::Type::getInt32Ty(ctx);
    Value *user_context = ConstantPointerNull::get(i8_ty->getPointerTo());

    llvm::Type *mxArray_ty = module->getTypeByName("struct.mxArray");
    internal_assert(mxArray_ty) << "Did not find mxArray in initial module";
    llvm::Type *mxArray_ptr_ty = mxArray_ty->getPointerTo();
    llvm::Type *mxArray_ptr_ptr_ty = mxArray_ptr_ty->getPointerTo();

    // Create the mexFunction function.
    // (http://www.mathworks.com/help/matlab/apiref/mexfunction.html)
    llvm::Type *mex_arg_types[] = {
        i32_ty,
        mxArray_ptr_ptr_ty,
        i32_ty,
        mxArray_ptr_ptr_ty,
    };
    FunctionType *mex_ty = FunctionType::get(void_ty, mex_arg_types, false);
    llvm::Function *mex = llvm::Function::Create(mex_ty, llvm::GlobalValue::ExternalLinkage, "mexFunction", module);
    BasicBlock *entry = BasicBlock::Create(ctx, "entry", mex);

    IRBuilder<> ir(ctx);
    ir.SetInsertPoint(entry);

    // Extract the argument values from the mexFunction.
    llvm::Function::arg_iterator mex_args = mex->arg_begin();
    Value *nlhs = mex_args++;
    Value *plhs = mex_args++;
    Value *nrhs = mex_args++;
    Value *prhs = mex_args++;

    Value *call_pipeline_args[] = {
        user_context,
        pipeline,
        metadata,
        nlhs,
        plhs,
        nrhs,
        prhs,
    };
    ir.CreateCall(call_pipeline, call_pipeline_args);
    ir.CreateRetVoid();

    return mex;
}

}  // namespace Internal
}  // namespace Halide
