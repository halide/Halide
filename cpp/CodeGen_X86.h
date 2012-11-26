#ifndef CODEGEN_X86_H
#define CODEGEN_X86_H

#include "CodeGen.h"

namespace HalideInternal {
    class CodeGen_X86 : public CodeGen {
    public:

        CodeGen_X86();

        void compile(Stmt stmt, string name, const vector<Argument> &args);

        static void test();

    protected:
        // Some useful types
        llvm::Type *i32x4, *i32x8;

        // Nodes that we handle specially
        virtual void visit(const Allocate *);        
    };

};

#endif
