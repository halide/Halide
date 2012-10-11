#ifndef HALIDE_FUNC_CONTENTS_H
#define HALIDE_FUNC_CONTENTS_H

#include "Func.h"
#include "Util.h"
#include <llvm-c/Core.h> // for LLVMModuleRef and LLVMValueRef
#include <llvm-c/ExecutionEngine.h>

namespace Halide {
    MLVal makeIdentity();

    struct FuncContents {
        FuncContents() :
            name(uniqueName('f')), guru(makeIdentity()), functionPtr(NULL) {}
        FuncContents(Type returnType) : 
            name(uniqueName('f')), returnType(returnType), guru(makeIdentity()), functionPtr(NULL) {}
      
        FuncContents(std::string name) : 
            name(name), guru(makeIdentity()), functionPtr(NULL) {}
        FuncContents(std::string name, Type returnType) : 
            name(name), returnType(returnType), guru(makeIdentity()), functionPtr(NULL) {}
      
        FuncContents(const char * name) : 
            name(name), guru(makeIdentity()), functionPtr(NULL) {}
        FuncContents(const char * name, Type returnType) : 
            name(name), returnType(returnType), guru(makeIdentity()), functionPtr(NULL) {}
        
        const std::string name;
        
        static LLVMExecutionEngineRef ee;
        static LLVMPassManagerRef fPassMgr;
        static LLVMPassManagerRef mPassMgr;
        // A handle to libcuda.so. Necessary if we don't link it in.
        static void *libCuda;
        // Was libcuda.so linked in already?
        static bool libCudaLinked;

        // The scalar value returned by the function
        Expr rhs;
        std::vector<Expr> args;
        Type returnType;

        // A handle to an update function
        shared_ptr<Func> update;
        
        /* A scheduling guru for this function. Actually a
         * partially-applied function that wraps a guru in the gurus
         * necessary to scheduling this function. */
        MLVal guru;

        // The compiled form of this function
        mutable void (*functionPtr)(void *);

        // Functions to assist realizing this function
        mutable void (*copyToHost)(buffer_t *);
        mutable void (*freeBuffer)(buffer_t *);
        mutable void (*errorHandler)(char *);

        MLVal applyGuru(MLVal);
        MLVal addDefinition(MLVal);
    };
}

#endif
