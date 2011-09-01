#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include "FImage.h"
#include <llvm-c/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Target/TargetData.h>

// declare the functions that live on the ml side

ML_FUNC1(makeIntImm);
ML_FUNC2(makeAdd);
ML_FUNC1(doPrint);
ML_FUNC1(makeVar);
ML_FUNC2(makeLoad); // buffer id, idx
ML_FUNC3(makeStore); // value, buffer id, idx
ML_FUNC1(doCompile); // stmt

namespace FImage {
    Expr Add(const Expr &a, const Expr &b) {
        return Expr(makeAdd(a.val, b.val));
    }

    Expr IntImm(int a) {
        return Expr(makeIntImm(MLVal::fromInt(a)));
    }

    Var::Var() : Expr(makeVar(MLVal::fromString("var"))) {
    }

    Var::Var(const char *a) : Expr(makeVar(MLVal::fromString(a))) {
    }

    void print(const Expr &a) {
        doPrint(a.val);
    }
    
    Expr::Expr(int x) : val(makeIntImm(MLVal::fromInt(x))), function_ptr(NULL) {
    }

    Expr::Expr(MLVal v) : val(v), function_ptr(NULL) {
    }

    Expr Expr::operator+(const Expr &b) {
        return Add(*this, b);
    }

    void run(const Expr &stmt, void *args) {        
        static llvm::ExecutionEngine *ee = NULL;

        if (!ee) {
            llvm::InitializeNativeTarget();
        }


        if (!stmt.function_ptr) {
            MLVal tuple = doCompile(stmt.val);
            LLVMModuleRef module = (LLVMModuleRef)Field(tuple.getValue(), 0);
            LLVMValueRef func = (LLVMValueRef)Field(tuple.getValue(), 1);
            llvm::Function *f = llvm::unwrap<llvm::Function>(func);
            llvm::Module *m = llvm::unwrap(module);

            if (!ee) {
                std::string errStr;
                ee = llvm::EngineBuilder(m).setErrorStr(&errStr).setOptLevel(llvm::CodeGenOpt::Aggressive).create();
                if (!ee) {
                    printf("Couldn't create execution engine: %s\n", errStr.c_str()); 
                    exit(1);
                }
            } else { 
                ee->addModule(m);
            }            
            /*
            // Set up the pass manager
            // TODO: Where do these things get cleaned up?
            llvm::FunctionPassManager *passMgr = new llvm::FunctionPassManager(m);
            passMgr->add(new llvm::TargetData(*ee->getTargetData()));
            // AliasAnalysis support for GVN
            passMgr->add(llvm::createBasicAliasAnalysisPass());
            // Peephole, bit-twiddling optimizations
            passMgr->add(llvm::createInstructionCombiningPass());
            // Reassociate expressions
            passMgr->add(llvm::createReassociatePass());
            // Eliminate common sub-expressions
            passMgr->add(llvm::createGVNPass());
            // Simplify CFG (delete unreachable blocks, etc.)
            passMgr->add(llvm::createCFGSimplificationPass());            
            passMgr->doInitialization();            
            */

            void *ptr = ee->getPointerToFunction(f);
            stmt.function_ptr = (void (*)(void*))ptr;
        }
        stmt.function_ptr(args);
    }

    Expr Load(int buf, const Expr &idx) {
        return makeLoad(MLVal::fromInt(buf), idx.val);
    }

    Expr Store(const Expr &val, int buf, const Expr &idx) {
        return makeStore(val.val, MLVal::fromInt(buf), idx.val);
    }

}
