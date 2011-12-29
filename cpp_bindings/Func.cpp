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

#include "../src/buffer.h"
#include "Func.h"
#include "Util.h"
#include "Var.h"
#include "Image.h"
#include "Uniform.h"
#include "elf.h"
#include <sstream>

namespace FImage {
    ML_FUNC2(makeVectorizeTransform);
    ML_FUNC2(makeUnrollTransform);
    ML_FUNC5(makeSplitTransform);
    ML_FUNC3(makeTransposeTransform);
    ML_FUNC4(makeChunkTransform);
    ML_FUNC3(makeRootTransform);
    ML_FUNC4(makeSerialTransform);
    ML_FUNC4(makeParallelTransform);
    
    ML_FUNC1(doConstantFold);
    
    ML_FUNC3(makeDefinition);
    ML_FUNC6(addScatterToDefinition);
    ML_FUNC0(makeEnv);
    ML_FUNC2(addDefinitionToEnv);
    
    ML_FUNC3(makeSchedule);
    ML_FUNC4(doLower);

    ML_FUNC1(printStmt);
    ML_FUNC1(printSchedule);
    ML_FUNC1(makeBufferArg); // name
    ML_FUNC2(makeScalarArg); // name, type
    ML_FUNC3(doCompile); // name, args, stmt
    ML_FUNC3(doCompileToFile); // name, args, stmt
    ML_FUNC2(makePair);
    ML_FUNC3(makeTriple);

    struct FuncRef::Contents {
        Contents(const Func &f) :
            f(f) {}
        Contents(const Func &f, const Expr &a) :
            f(f), args {a} {}
        Contents(const Func &f, const Expr &a, const Expr &b) :
            f(f), args {a, b} {}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c) :
            f(f), args {a, b, c} {}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) :
            f(f), args {a, b, c, d} {}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) :
            f(f), args {a, b, c, d, e} {}
        Contents(const Func &f, const std::vector<Expr> &args) : f(f), args(args) {}

        // A pointer to the function object that this lhs defines.
        Func f;
        std::vector<Expr> args;
    };

    struct Func::Contents {
        Contents() :
            name(uniqueName('f')), functionPtr(NULL), tracing(false) {}
        Contents(Type returnType) : 
            name(uniqueName('f')), returnType(returnType), functionPtr(NULL), tracing(false) {}
      
        Contents(std::string name) : 
            name(name), functionPtr(NULL), tracing(false) {}
        Contents(std::string name, Type returnType) : 
            name(name), returnType(returnType), functionPtr(NULL), tracing(false) {}
      
        Contents(const char * name) : 
            name(name), functionPtr(NULL), tracing(false) {}
        Contents(const char * name, Type returnType) : 
            name(name), returnType(returnType), functionPtr(NULL), tracing(false) {}
        
        const std::string name;
        
        // The scalar value returned by the function
        Expr rhs;
        std::vector<Expr> args;
        MLVal arglist;
        Type returnType;
        
        /* The ML definition object (name, return type, argnames, body)
           The body here evaluates the function over an entire range,
           and the arg list will include a min and max value for every
           free variable. */
        MLVal definition;
        
        /* A list of schedule transforms to apply when realizing. These should be
           partially applied ML functions that map a schedule to a schedule. */
        std::vector<MLVal> scheduleTransforms;
        
        // The compiled form of this function
        mutable void (*functionPtr)(void *); 
        
        // Should this function be compiled with tracing enabled
        bool tracing;

        // The region over which the function is evaluated
        std::vector<Uniform<int>> outputSize;
    };    

    Range operator*(const Range &a, const Range &b) {
        Range region;
        region.range.resize(a.range.size() + b.range.size());
        for (size_t i = 0; i < a.range.size(); i++) {
            region.range[i] = a.range[i];
        }
        for (size_t i = 0; i < b.range.size(); i++) {
            region.range[a.range.size() + i] = b.range[i];
        }
        return region;
    }

    FuncRef::FuncRef(const Func &f) :
        contents(new FuncRef::Contents(f)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a) : 
        contents(new FuncRef::Contents(f, a)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b) :
        contents(new FuncRef::Contents(f, a, b)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c) :
        contents(new FuncRef::Contents(f, a, b, c)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) : 
        contents(new FuncRef::Contents(f, a, b, c, d)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) : 
        contents(new FuncRef::Contents(f, a, b, c, d, e)) {
    }

    FuncRef::FuncRef(const Func &f, const std::vector<Expr> &args) :
        contents(new FuncRef::Contents(f, args)) {
    }

    void FuncRef::operator=(const Expr &e) {
        contents->f.define(contents->args, e);
    }

    void FuncRef::operator+=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined())             
            contents->f.define(gather_args, Cast(e.type(), 0));
        contents->f.define(contents->args, contents->f(contents->args) + e);
    }

    void FuncRef::operator*=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined()) 
            contents->f.define(gather_args, Cast(e.type(), 1));
        contents->f.define(contents->args, contents->f(contents->args) * e);
    }

    const Func &FuncRef::f() const {
        return contents->f;
    }

    const std::vector<Expr> &FuncRef::args() const {
        return contents->args;
    }

    Func::Func() : contents(new Contents()) {
    }
 
    Func::Func(const std::string &name) : contents(new Contents(name)) {
    }

    Func::Func(const char *name) : contents(new Contents(name)) {
    }

    Func::Func(const Type &t) : contents(new Contents(t)) {
    }

    Func::Func(const std::string &name, Type t) : contents(new Contents(name, t)) {
    }

    Func::Func(const char *name, Type t) : contents(new Contents(name, t)) {
    }

    bool Func::operator==(const Func &other) const {
        return other.contents == contents;
    }

    const Expr &Func::rhs() const {
        return contents->rhs;
    }

    const Type &Func::returnType() const {
        return contents->returnType;
    }

    const std::vector<Expr> &Func::args() const {
        return contents->args;
    }

    const std::string &Func::name() const { 
        return contents->name;
    }

    const std::vector<MLVal> &Func::scheduleTransforms() const {
        return contents->scheduleTransforms;
    }

    void Func::define(const std::vector<Expr> &_args, const Expr &r) {
        printf("Defining %s\n", name().c_str());

        // Make sure the environment exists
        if (!environment) {
            printf("Creating environment\n");
            environment = new MLVal(makeEnv());
        }

        // Make a local copy of the argument list
        std::vector<Expr> args = _args;

        // Add any implicit arguments 
        //printf("Adding %d implicit arguments\n", r.implicitArgs());
        for (int i = 0; i < r.implicitArgs(); i++) {
            std::ostringstream ss;
            ss << "iv"; // implicit var
            ss << i;
            args.push_back(Var(ss.str()));
        }

        //printf("Defining %s\n", name().c_str());

        // Are we talking about a scatter or a gather here?
        bool gather = true;
        printf("%u args %u rvars\n", (unsigned)args.size(), (unsigned)r.rvars().size());
        for (size_t i = 0; i < args.size(); i++) {            
            if (!args[i].isVar()) {
                gather = false;
            }
        }
        if (r.rvars().size() > 0) gather = false;

        if (gather) {
            printf("Gather definition for %s\n", name().c_str());
            contents->rhs = r;            
            contents->returnType = r.type();
            contents->args = args;
            contents->arglist = makeList();
            for (size_t i = args.size(); i > 0; i--) {
                contents->arglist = addToList(contents->arglist, (contents->args[i-1].vars()[0].name()));
            }
             
            contents->definition = makeDefinition((name()), contents->arglist, rhs().node());
            
            *environment = addDefinitionToEnv(*environment, contents->definition);

        } else {
            printf("Scatter definition for %s\n", name().c_str());
            assert(rhs().isDefined());

            MLVal update_args = makeList();
            for (size_t i = args.size(); i > 0; i--) {
                update_args = addToList(update_args, args[i-1].node());
                contents->rhs.child(args[i-1]);
            }                                                            

            contents->rhs.child(r);
           
            MLVal reduction_args = makeList();
            const std::vector<RVar> &rvars = contents->rhs.rvars();
            for (size_t i = rvars.size(); i > 0; i--) {
                reduction_args = addToList(reduction_args, 
                                           makeTriple(rvars[i-1].name(), 
                                                      rvars[i-1].min().node(), 
                                                      rvars[i-1].size().node()));
            }
            
            printf("Adding scatter definition for %s\n", name().c_str());
            // There should already be a gathering definition of this function. Add the scattering term.
            *environment = addScatterToDefinition(*environment, name(), uniqueName('p'), 
                                                  update_args, r.node(), reduction_args);
        }
    }

    void Func::trace() {
        contents->tracing = true;
    }

    void Func::vectorize(const Var &v) {
        MLVal t = makeVectorizeTransform((name()),
                                         (v.name()));
        contents->scheduleTransforms.push_back(t);
    }

    void Func::vectorize(const Var &v, int factor) {
        if (factor == 1) return;
        Var vi;
        split(v, v, vi, factor);
        vectorize(vi);        
    }

    void Func::unroll(const Var &v) {
        MLVal t = makeUnrollTransform((name()),
                                      (v.name()));        
        contents->scheduleTransforms.push_back(t);
    }

    void Func::unroll(const Var &v, int factor) {
        if (factor == 1) return;
        Var vi;
        split(v, v, vi, factor);
        unroll(vi);
    }


    void Func::range(const Var &v, const Expr &min, const Expr &size, bool serial) {
        MLVal t;
        if (serial) {
            t = makeSerialTransform((name()),
                                    (v.name()),
                                    min.node(),
                                    size.node());
        } else {
            t = makeParallelTransform((name()),
                                      (v.name()),
                                      min.node(),
                                      size.node());
        }
        contents->scheduleTransforms.push_back(t);
    }

    void Func::split(const Var &old, const Var &newout, const Var &newin, int factor) {
        MLVal t = makeSplitTransform((name()),
                                     (old.name()),
                                     (newout.name()),
                                     (newin.name()),
                                     (factor));
        contents->scheduleTransforms.push_back(t);
    }

    void Func::transpose(const Var &outer, const Var &inner) {
        MLVal t = makeTransposeTransform((name()),
                                         (outer.name()),
                                         (inner.name()));
        contents->scheduleTransforms.push_back(t);
    }

    void Func::chunk(const Var &caller_var, const Range &region) {
        MLVal r = makeList();
        for (size_t i = region.range.size(); i > 0; i--) {
            r = addToList(r, makePair(region.range[i-1].first.node(), region.range[i-1].second.node()));
        }

        MLVal t = makeChunkTransform(name(),
                                     caller_var.name(),
                                     contents->arglist,
                                     r);
        contents->scheduleTransforms.push_back(t);
    }
  
    void Func::chunk(const Var &caller_var) {
        MLVal t = makeChunkTransform(name(), caller_var.name(), contents->arglist, makeList());
        contents->scheduleTransforms.push_back(t);
    }

    void Func::root(const Range &region) {
        MLVal r = makeList();
        for (size_t i = region.range.size(); i > 0; i--) {
            r = addToList(r, makePair(region.range[i-1].first.node(), region.range[i-1].second.node()));
        }

        MLVal t = makeRootTransform(name(),
                                    contents->arglist,
                                    r);
        contents->scheduleTransforms.push_back(t);        
    }

    void Func::root() {
        MLVal t = makeRootTransform(name(), contents->arglist, makeList());
        contents->scheduleTransforms.push_back(t);
    }

    DynImage Func::realize(int a) {
        DynImage im(returnType(), a);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b) {
        DynImage im(returnType(), a, b);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b, int c) {
        DynImage im(returnType(), a, b, c);
        realize(im);
        return im;
    }


    DynImage Func::realize(int a, int b, int c, int d) {
        DynImage im(returnType(), a, b, c, d);
        realize(im);
        return im;
    }

    void Func::compile() {
        static llvm::ExecutionEngine *ee = NULL;
        static llvm::FunctionPassManager *passMgr = NULL;

        if (!ee) {
            llvm::InitializeNativeTarget();
        }

        // Make a region to evaluate this over
        contents->outputSize.resize(args().size());
        MLVal sizes = makeList();        
        for (size_t i = args().size(); i > 0; i--) {                
            sizes = addToList(sizes, Expr(contents->outputSize[i-1]).node());
        }
        
        MLVal sched = makeSchedule((name()),
                                   sizes,
                                   *Func::environment);
        
        printf("Transforming schedule...\n");
        for (size_t i = 0; i < contents->scheduleTransforms.size(); i++) {
            sched = contents->scheduleTransforms[i](sched);
        }
        
        for (size_t i = 0; i < rhs().funcs().size(); i++) {
            const Func &f = rhs().funcs()[i];
            // Don't consider recursive dependencies for the
            // purpose of applying schedule transformations. We
            // already did that above.
            if (f == *this) continue;
            for (size_t j = 0; j < f.scheduleTransforms().size(); j++) {
                MLVal t = f.scheduleTransforms()[j];
                sched = t(sched);
            }
        }
        
        printf("Done transforming schedule\n");
        printSchedule(sched);
        
        MLVal stmt = doLower((name()), 
                             *Func::environment,
                             sched, contents->tracing);
        
        // Create a function around it with the appropriate number of args
        printf("\nMaking function...\n");           
        MLVal args = makeList();
        args = addToList(args, makeBufferArg("result"));
        for (size_t i = rhs().uniformImages().size(); i > 0; i--) {
            MLVal arg = makeBufferArg(rhs().uniformImages()[i-1].name());
            args = addToList(args, arg);
        }
        for (size_t i = rhs().images().size(); i > 0; i--) {
            MLVal arg = makeBufferArg(rhs().images()[i-1].name());
            args = addToList(args, arg);
        }
        for (size_t i = rhs().uniforms().size(); i > 0; i--) {
            const DynUniform &u = rhs().uniforms()[i-1];
            MLVal arg = makeScalarArg(u.name(), u.type().mlval);
            args = addToList(args, arg);
        }
        for (size_t i = contents->outputSize.size(); i > 0; i--) {
            const DynUniform &u = contents->outputSize[i-1];
            MLVal arg = makeScalarArg(u.name(), u.type().mlval);
            args = addToList(args, arg);
        }
        
        printf("compiling IR -> ll\n");
        MLVal tuple = doCompile(name(), args, stmt);
        
        doCompileToFile(name(), args, stmt);

        printf("Extracting the resulting module and function\n");
        MLVal first, second;
        MLVal::unpackPair(tuple, first, second);
        //LLVMModuleRef module = *((LLVMModuleRef *)(first.asVoidPtr()));
        //LLVMValueRef func = *((LLVMValueRef *)(second.asVoidPtr()));
        LLVMModuleRef module = (LLVMModuleRef)(first.asVoidPtr());
        LLVMValueRef func = (LLVMValueRef)(second.asVoidPtr());
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
        
        std::string functionName = name() + "_c_wrapper";
        llvm::Function *inner = m->getFunction(functionName.c_str());
        
        if (!inner) {
            printf("Could not find function %s", functionName.c_str());
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
        contents->functionPtr = (void (*)(void*))ptr;
        
        printf("dumping machine code to file...\n");
        saveELF("generated.o", ptr, 8192);            
        printf("Done dumping machine code to file\n");
        
    }

    template <class image_t>
    static buffer_t BufferOfImage(image_t& im) {
        buffer_t buf;
        buf.host = im.data();
		static const int max_dim = 4;
		int dim = 0;
        while (dim < im.dimensions()) {
            buf.dims[dim] = size_t(im.size(dim));
			dim++;
        }
		while (dim < max_dim) {
			buf.dims[dim] = 1;
			dim++;
		}
        buf.elem_size = im.type().bits / 8;
        buf.dev = 0;
        buf.dev_dirty = false;

        printf("BufferOfImage: %p (%zux%zux%zu) %zu bytes\n", buf.host, buf.dims[0], buf.dims[1], buf.dims[2], buf.elem_size);

        return buf;
    }

    void Func::realize(const DynImage &im) {

        if (!contents->functionPtr) compile();

        printf("Constructing argument list...\n");
        static void *arguments[256];
        static buffer_t buffers[256];
        size_t j = 0;
        size_t k = 0;
        for (size_t i = 0; i < contents->outputSize.size(); i++) {
            // Set and use the uniform in one place. It's a little
            // useless because nobody ever actually uses the value
            // of the uniform, but better to be consistent in case it
            // comes up later.
            contents->outputSize[i] = im.size(i);
            arguments[j++] = contents->outputSize[i].data();
        }
        for (size_t i = 0; i < rhs().uniforms().size(); i++) {
            arguments[j++] = rhs().uniforms()[i].data();
        }
        for (size_t i = 0; i < rhs().images().size(); i++) {
            buffers[k++] = BufferOfImage(rhs().images()[i]);
            arguments[j++] = buffers + (k-1);
        }       
        for (size_t i = 0; i < rhs().uniformImages().size(); i++) {
            buffers[k++] = BufferOfImage(rhs().uniformImages()[i]);
            arguments[j++] = buffers + (k-1);
        }
        buffers[k] = BufferOfImage(im);
        arguments[j] = buffers + k;

        printf("Args: ");
        for (size_t i = 0; i <= j; i++) {
            printf("%p ", arguments[i]);
        }
        printf("\n");

        printf("Calling function at %p\n", contents->functionPtr); 
        contents->functionPtr(&arguments[0]); 
    }

    MLVal *Func::environment = NULL;

}
