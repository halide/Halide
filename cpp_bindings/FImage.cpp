#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include <fcntl.h>

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
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Support/raw_ostream.h>

#include "elf.h"

// declare the functions that live on the ml side

ML_FUNC1(makeIntImm);
ML_FUNC2(makeAdd);
ML_FUNC2(makeSub);
ML_FUNC2(makeMul);
ML_FUNC2(makeDiv);
ML_FUNC2(makeEQ);
ML_FUNC2(makeNE);
ML_FUNC2(makeLT);
ML_FUNC2(makeGT);
ML_FUNC2(makeGE);
ML_FUNC2(makeLE);
ML_FUNC3(makeSelect);
ML_FUNC1(doPrint);
ML_FUNC1(makeVar);
ML_FUNC2(makeLoad); // buffer id, idx
ML_FUNC3(makeStore); // value, buffer id, idx
ML_FUNC1(makeBufferArg); // name
ML_FUNC1(doCompile); // stmt
ML_FUNC0(makeArgList); 
ML_FUNC2(addArgToList); // old arg list, new arg -> new list
ML_FUNC2(makeFunction); // stmt, arg list
ML_FUNC4(makeMap); // var name, min, max, stmt
ML_FUNC2(doVectorize);
ML_FUNC2(doUnroll);
ML_FUNC5(doSplit);
ML_FUNC1(doConstantFold);

namespace FImage {

    template<typename T>
    void unify(std::vector<T *> &a, const std::vector<T *> &b) {
        for (size_t i = 0; i < b.size(); i++) {
            bool is_in_a = false;
            for (size_t j = 0; j < a.size(); j++) {
                if (a[j] == b[i]) is_in_a = true;
            }
            if (!is_in_a) a.push_back(b[i]);
        }
    }

    // An Expr is a wrapper around the node structure used by the compiler
    Expr::Expr() {}

    Expr::Expr(MLVal n) : node(n) {}

    Expr::Expr(int32_t val) {
        node = makeIntImm(MLVal::fromInt(val));
    }

    // declare that this node has a child for bookkeeping
    void Expr::child(const Expr &c) {
        unify(args, c.args);
        unify(vars, c.vars);
    }

    void Expr::operator+=(const Expr & other) {
        node = makeAdd(node, other.node);
        child(other);
    }
    
    void Expr::operator*=(const Expr & other) {
        node = makeMul(node, other.node);
        child(other);
    }

    void Expr::operator/=(const Expr & other) {
        node = makeDiv(node, other.node);
        child(other);
    }

    void Expr::operator-=(const Expr & other) {
        node = makeSub(node, other.node);
        child(other);
    }


    Expr operator+(const Expr & a, const Expr & b) {
        Expr e(makeAdd(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator-(const Expr & a, const Expr & b) {
        Expr e(makeSub(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator*(const Expr & a, const Expr & b) {
        Expr e(makeMul(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator/(const Expr & a, const Expr & b) {
        Expr e(makeDiv(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator>(const Expr & a, const Expr & b) {
        Expr e(makeGT(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<(const Expr & a, const Expr & b) {
        Expr e(makeLT(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator>=(const Expr & a, const Expr & b) {
        Expr e(makeGE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<=(const Expr & a, const Expr & b) {
        Expr e(makeLE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator!=(const Expr & a, const Expr & b) {
        Expr e(makeNE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator==(const Expr & a, const Expr & b) {
        Expr e(makeEQ(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr select(const Expr & cond, const Expr & thenCase, const Expr & elseCase) {
        Expr e(makeSelect(cond.node, thenCase.node, elseCase.node));
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    // Print out an expression
    void Expr::debug() {
        doPrint(node);
    }

    Var::Var(int a, int b) : _min(a), _max(b), _vectorize(1), _unroll(1) {
        snprintf(_name, sizeof(_name), "var%d", _instances++);
        node = makeVar(MLVal::fromString(_name));
        vars.push_back(this);
    }

    // Make an MemRef reference to a particular pixel. It can be used as an
    // assignment target or cast to a load operation. In the future it may
    // also serve as a marker for a site in an expression that can be
    // fused.
    MemRef::MemRef(Image *im_, const Expr & a) : im(im_), function_ptr(NULL) {
        
        // If you upcast this to an Expr it gets treated as a load
        addr = a * im->stride[0];
        // TODO: replace this name with topological position
        node = makeLoad(MLVal::fromString(im->name()), addr.node);

        args.push_back(im);
        child(a);

        indices.resize(1);
        indices[0] = a;
    }

    MemRef::MemRef(Image *im_, const Expr & a, const Expr & b) : im(im_), function_ptr(NULL) {
        
        // If you upcast this to an Expr it gets treated as a load
        addr = a * im->stride[0] + b * im->stride[1];
        node = makeLoad(MLVal::fromString(im->name()), addr.node);

        args.push_back(im);
        child(a);
        child(b);

        printf("%d %d %d\n", a.args.size(), b.args.size(), args.size());
        
        indices.resize(2);
        indices[0] = a;
        indices[1] = b;
    }

    MemRef::MemRef(Image *im_, const Expr & a, const Expr & b, const Expr & c) : im(im_), function_ptr(NULL) {
        
        // If you upcast this to an Expr it gets treated as a load
        addr = a * im->stride[0] + b * im->stride[1] + c * im->stride[2];
        node = makeLoad(MLVal::fromString(im->name()), addr.node);

        args.push_back(im);
        child(a);
        child(b);
        child(c);

        indices.resize(3);
        indices[0] = a;
        indices[1] = b;
        indices[2] = c;
    }
    
    MemRef::MemRef(Image *im_, const Expr & a, const Expr & b, const Expr & c, const Expr & d) : im(im_), function_ptr(NULL) {
        
        // If you upcast this to an Expr it gets treated as a load
        addr = a * im->stride[0] + b * im->stride[1] + c * im->stride[2] + d * im->stride[3];
        node = makeLoad(MLVal::fromString(im->name()), addr.node);

        args.push_back(im);
        child(a);
        child(b);
        child(c);
        child(d);

        indices.resize(4);
        indices[0] = a;
        indices[1] = b;
        indices[2] = c;
        indices[3] = d;
    }

    void MemRef::operator=(const Expr &e) {
        // We were a load - convert to a store instead
        node = makeStore(e.node, MLVal::fromString(im->name()), addr.node);

        child(e);

        // Add this to the list of definitions of im
        printf("Adding a definition\n");
        im->definitions.push_back(*this);
    }

    Image::Image(uint32_t a) {
        size.resize(1);
        stride.resize(1);
        size[0] = a;
        stride[0] = 1;
        // TODO: enforce alignment, lazy allocation, etc, etc
        buffer.reset(new std::vector<float>(a + 8));
        data = &(*buffer)[0] + 4;
        snprintf(_name, sizeof(_name), "im%d", _instances++);
    }

    Image::Image(uint32_t a, uint32_t b) {
        size.resize(2);
        stride.resize(2);
        size[0] = a;
        size[1] = b;
        stride[0] = 1;
        stride[1] = a;
        buffer.reset(new std::vector<float>(a*b + 8));
        data = &(*buffer)[0] + 4;
        snprintf(_name, sizeof(_name), "im%d", _instances++);
    }
    
    Image::Image(uint32_t a, uint32_t b, uint32_t c) {
        size.resize(3);
        stride.resize(3);
        size[0] = a;
        size[1] = b;
        size[2] = c;
        stride[0] = 1;
        stride[1] = a;
        stride[2] = a*b;
        buffer.reset(new std::vector<float>(a*b*c + 8));
        data = &(*buffer)[0] + 4;
        snprintf(_name, sizeof(_name), "im%d", _instances++);
    }

    Image::Image(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        size.resize(4);
        stride.resize(4);
        size[0] = a;
        size[1] = b;
        size[2] = c;
        size[3] = d;
        stride[0] = 1;
        stride[1] = a;
        stride[2] = a*b;
        stride[3] = a*b*c;
        buffer.reset(new std::vector<float>(a*b*c*d + 8));
        data = &(*buffer)[0] + 4;
        snprintf(_name, sizeof(_name), "im%d", _instances++);
    }
    
    Image::~Image() {
        //delete[] (data-4);
    }

    // Generate an MemRef reference to a (computed) location in this image
    // that can be used as an assignment target, and can also be cast to
    // the Expr version (a load of a computed address)
    MemRef Image::operator()(const Expr & a) {
        return MemRef(this, a);
    }

    MemRef Image::operator()(const Expr & a, const Expr & b) {
        return MemRef(this, a, b);
    }

    MemRef Image::operator()(const Expr & a, const Expr & b, const Expr & c) {
        return MemRef(this, a, b, c);
    }

    MemRef Image::operator()(const Expr & a, const Expr & b, const Expr & c, const Expr & d) {
        return MemRef(this, a, b, c, d);
    }

    // Print out a particular definition. We assume the MemRef has already been assigned to.
    void MemRef::debug() {
        printf("[");
        for (size_t i = 0; i < indices.size(); i++) {
            doPrint(indices[i].node);
            printf(", ");
        }
        printf("\b\b]\n");
    }


    // Print out all the definitions of this Image
    void Image::debug() {
        for (size_t i = 0; i < definitions.size(); i++) {
            definitions[i].debug();
        }
    }

    Image &Image::evaluate() {
        // Just evaluate the first definition
        MemRef &stmt = definitions[0];

        static llvm::ExecutionEngine *ee = NULL;
        static llvm::FunctionPassManager *passMgr = NULL;

        if (!ee) {
            llvm::InitializeNativeTarget();
        }

        if (!stmt.function_ptr) {

            // Surround it with the appropriate number of maps
            MLVal loop = stmt.node;
            for (size_t i = 0; i < stmt.vars.size(); i++) {
                printf("Wrapping statement in a loop...\n");
                loop = makeMap(MLVal::fromString(stmt.vars[i]->name()),
                               Expr(stmt.vars[i]->min()).node,
                               Expr(stmt.vars[i]->max()).node,
                               loop);
            }

            // Perform any requested transformations
            for (size_t i = 0; i < stmt.vars.size(); i++) {
                int v = stmt.vars[i]->vectorize();
                if (v > 1) {
                    printf("Before vectorizing:\n");
                    doPrint(loop);
                    MLVal outer = MLVal::fromString(stmt.vars[i]->name());
                    MLVal inner = MLVal::fromString(std::string(stmt.vars[i]->name()) + "_inner");
                    loop = doSplit(outer, outer, inner, MLVal::fromInt(v), loop);
                    loop = doConstantFold(loop);
                    loop = doVectorize(inner, loop);
                    loop = doConstantFold(loop);
                    printf("After vectorizing:\n");
                    doPrint(loop);
                }
                int u = stmt.vars[i]->unroll();
                if (u > 1) {
                    printf("Before unrolling:\n");
                    doPrint(loop);
                    MLVal outer = MLVal::fromString(stmt.vars[i]->name());
                    MLVal inner = MLVal::fromString(std::string(stmt.vars[i]->name()) + "_inner");
                    loop = doSplit(outer, outer, inner, MLVal::fromInt(u), loop);
                    loop = doConstantFold(loop);
                    loop = doUnroll(inner, loop);
                    loop = doConstantFold(loop);
                    printf("After unrolling:\n");
                    doPrint(loop);
                }                    
            }

            // Create a function around it with the appropriate number of args
            MLVal args = makeArgList();
            for (size_t i = 0; i < stmt.args.size(); i++) {
                MLVal arg = makeBufferArg(MLVal::fromString(stmt.args[i]->name()));
                args = addArgToList(args, arg);
            }
            
            printf("\nMaking function...\n");
            
            MLVal function = makeFunction(args, loop);

            doPrint(loop);

            printf("compiling IR -> ll\n");
            MLVal tuple = doCompile(function);

            printf("Extracting the resulting module and function\n");
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

                // Set up the pass manager
                passMgr = new llvm::FunctionPassManager(m);

            } else { 
                ee->addModule(m);
            }            

            llvm::Function *inner = m->getFunction("_im_main");

            if (!inner) {
                printf("Could not find function _im_main");
                exit(1);
            }

            printf("optimizing ll...\n");

            std::string errstr;
            llvm::raw_fd_ostream stdout("passes.txt", errstr);
  
            passMgr->add(new llvm::TargetData(*ee->getTargetData()));
            //passMgr->add(llvm::createPrintFunctionPass("*** Before optimization ***", &stdout));

            // AliasAnalysis support for GVN
            passMgr->add(llvm::createBasicAliasAnalysisPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After basic alias analysis ***", &stdout));

            // Reassociate expressions
            passMgr->add(llvm::createReassociatePass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After reassociate ***", &stdout));

            // Simplify CFG (delete unreachable blocks, etc.)
            passMgr->add(llvm::createCFGSimplificationPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After CFG simplification ***", &stdout));

            // Eliminate common sub-expressions
            passMgr->add(llvm::createGVNPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After GVN pass ***", &stdout));

            // Peephole, bit-twiddling optimizations
            passMgr->add(llvm::createInstructionCombiningPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After instruction combining ***", &stdout));
            
            passMgr->doInitialization();

            if (passMgr->run(*inner)) {
                printf("optimization did something.\n");
            } else {
                printf("optimization did nothing.\n");
            }

            passMgr->doFinalization();

            printf("compiling ll -> machine code...\n");
            void *ptr = ee->getPointerToFunction(f);
            stmt.function_ptr = (void (*)(void*))ptr;

            printf("dumping machine code to file...\n");
            saveELF("generated.o", ptr, 8192);            
            printf("Done dumping machine code to file\n");
        }

        printf("Constructing argument list...\n");
        static void *arguments[256];
        for (size_t i = 0; i < stmt.args.size(); i++) {
            arguments[i] = (void *)stmt.args[i]->data;
        }

        printf("Calling function...\n"); 
        stmt.function_ptr(&arguments[0]); 

        return *this;
    }


    int Image::_instances = 0;
    int Var::_instances = 0;
}
