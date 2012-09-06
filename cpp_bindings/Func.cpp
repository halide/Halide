#include <llvm-c/Core.h> // for LLVMModuleRef and LLVMValueRef
#include <llvm/ExecutionEngine/GenericValue.h>
//#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Transforms/IPO.h>
#include <sys/time.h>

#include "../src/buffer.h"
#include "Func.h"
#include "Util.h"
#include "Var.h"
#include "Image.h"
#include "Uniform.h"
#include <sstream>

#include <dlfcn.h>
#include <unistd.h>

namespace Halide {
    
    extern "C" { typedef struct CUctx_st *CUcontext; }
    CUcontext cuda_ctx = 0;
    
    bool use_gpu() {
        char* target = getenv("HL_TARGET");
        return (target != NULL && strncasecmp(target, "ptx", 3) == 0);
    }

    bool use_avx() {
	char *target = getenv("HL_TARGET");
	if (target == NULL || strncasecmp(target, "x86_64", 6) == 0) {
        #ifdef __x86_64__
	    int func = 1, ax, bx, cx, dx;
	    __asm__ __volatile__ ("cpuid":				
				  "=a" (ax), 
				  "=b" (bx), 
				  "=c" (cx), 
				  "=d" (dx) : 
				  "a" (func));	    
	    // bit 28 of ecx indicates avx support
	    return cx & 0x10000000;
	    #endif
	}
	return false;
    }
    
    ML_FUNC2(makeVectorizeTransform);
    ML_FUNC2(makeUnrollTransform);
    ML_FUNC4(makeBoundTransform);
    ML_FUNC5(makeSplitTransform);
    ML_FUNC3(makeTransposeTransform);
    ML_FUNC2(makeChunkTransform);
    ML_FUNC1(makeRootTransform);
    ML_FUNC2(makeParallelTransform);
    ML_FUNC2(makeRandomTransform);
    
    ML_FUNC1(doConstantFold);
  
    ML_FUNC3(makeDefinition);
    ML_FUNC6(addScatterToDefinition);
    ML_FUNC0(makeEnv);
    ML_FUNC2(addDefinitionToEnv);
    
    ML_FUNC4(makeSchedule);
    ML_FUNC3(doLower);

    ML_FUNC0(makeNoviceGuru);
    ML_FUNC1(loadGuruFromFile);
    ML_FUNC2(saveGuruToFile);

    ML_FUNC1(printStmt);
    ML_FUNC1(printSchedule);
    ML_FUNC1(makeBufferArg); // name
    ML_FUNC2(makeScalarArg); // name, type
    ML_FUNC3(doCompile); // name, args, stmt
    ML_FUNC3(doCompileToFile); // name, args, stmt
    ML_FUNC2(makePair);
    ML_FUNC3(makeTriple);

    ML_FUNC1(serializeStmt); // stmt
    ML_FUNC3(serializeEntry); // name, args, stmt

    struct FuncRef::Contents {
        Contents(const Func &f) :
            f(f) {}
        Contents(const Func &f, const Expr &a) :
            f(f), args {a} {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b) :
            f(f), args {a, b} {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c) :
            f(f), args {a, b, c} {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) :
            f(f), args {a, b, c, d} {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) :
            f(f), args {a, b, c, d, e} {fixArgs();}
        Contents(const Func &f, const std::vector<Expr> &args) : f(f), args(args) {fixArgs();}

        void fixArgs() {
            for (size_t i = 0; i < args.size(); i++) {
                if (args[i].type() != Int(32)) {
                    args[i] = cast<int>(args[i]);
                }
            }
        }

        // A pointer to the function object that this lhs defines.
        Func f;
        std::vector<Expr> args;
    };

    struct Func::Contents {
        Contents() :
            name(uniqueName('f')), functionPtr(NULL) {}
        Contents(Type returnType) : 
            name(uniqueName('f')), returnType(returnType), functionPtr(NULL) {}
      
        Contents(std::string name) : 
            name(name), functionPtr(NULL) {}
        Contents(std::string name, Type returnType) : 
            name(name), returnType(returnType), functionPtr(NULL) {}
      
        Contents(const char * name) : 
            name(name), functionPtr(NULL) {}
        Contents(const char * name, Type returnType) : 
            name(name), returnType(returnType), functionPtr(NULL) {}
        
        const std::string name;
        
        static llvm::ExecutionEngine *ee;
        static llvm::FunctionPassManager *fPassMgr;
        static llvm::PassManager *mPassMgr;
        // A handle to libcuda.so. Necessary if we don't link it in.
        static void *libCuda;
        // Was libcuda.so linked in already?
        static bool libCudaLinked;

        // The scalar value returned by the function
        Expr rhs;
        std::vector<Expr> args;
        MLVal arglist;
        Type returnType;

        // A handle to an update function
        std::unique_ptr<Func> update;
        
        /* The ML definition object (name, return type, argnames, body)
           The body here evaluates the function over an entire range,
           and the arg list will include a min and max value for every
           free variable. */
        MLVal definition;
        
        /* A list of schedule transforms to apply when realizing. These should be
           partially applied ML functions that map a schedule to a schedule. */
        std::vector<MLVal> scheduleTransforms;        
        MLVal applyScheduleTransforms(MLVal);

        // The compiled form of this function
        mutable void (*functionPtr)(void *);

        // Functions to assist realizing this function
        mutable void (*copyToHost)(buffer_t *);
        mutable void (*freeBuffer)(buffer_t *);
        mutable void (*errorHandler)(char *);

    };

    llvm::ExecutionEngine *Func::Contents::ee = NULL;
    llvm::FunctionPassManager *Func::Contents::fPassMgr = NULL;
    llvm::PassManager *Func::Contents::mPassMgr = NULL;
    void *Func::Contents::libCuda = NULL;
    bool Func::Contents::libCudaLinked = false;
    
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

    FuncRef::FuncRef(const FuncRef &other) :
        contents(other.contents) {
    }

    void FuncRef::operator=(const Expr &e) {
        contents->f.define(contents->args, e);
    }

    void FuncRef::operator+=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined()) {
            Expr init = cast(e.type(), 0);
            init.addImplicitArgs(e.implicitArgs());
            contents->f.define(gather_args, init);
        }
        contents->f.define(contents->args, contents->f(contents->args) + e);
    }

    void FuncRef::operator*=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined()) {
            Expr init = cast(e.type(), 1);
            init.addImplicitArgs(e.implicitArgs());
            contents->f.define(gather_args, init);
        }
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
    
    const Var &Func::arg(int i) const {
        const Expr& e = args()[i];
        assert (e.isVar());
        return e.vars()[0];
    }

    const std::string &Func::name() const { 
        return contents->name;
    }

    const std::vector<MLVal> &Func::scheduleTransforms() const {
        return contents->scheduleTransforms;
    }

    void Func::define(const std::vector<Expr> &_args, const Expr &r) {
        //printf("Defining %s\n", name().c_str());

        // Make sure the environment exists
        if (!environment) {
            //printf("Creating environment\n");
            environment = new MLVal(makeEnv());
        }

        // Make a local copy of the argument list
        std::vector<Expr> args = _args;

        // Add any implicit arguments 
        //printf("Adding %d implicit arguments\n", r.implicitArgs());
        for (int i = 0; i < r.implicitArgs(); i++) {
            args.push_back(Var(std::string("iv") + int_to_str(i))); // implicit var. Connelly: ostringstream broken in Python binding, use string + instead
        }
        
        //printf("Defining %s\n", name().c_str());

        // Are we talking about a scatter or a gather here?
        bool gather = true;
        //printf("%u args %u rvars\n", (unsigned)args.size(), (unsigned)r.rdom().dimensions());
        for (size_t i = 0; i < args.size(); i++) {            
            if (!args[i].isVar()) {
                gather = false;
            }
        }
        if (r.rdom().dimensions() > 0) gather = false;

        if (gather) {
        //printf("Gather definition for %s\n", name().c_str());
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
            //printf("Scatter definition for %s\n", name().c_str());
            assert(rhs().isDefined() && "Must provide a base-case definition for function before the reduction case");            

            MLVal update_args = makeList();
            for (size_t i = args.size(); i > 0; i--) {
                update_args = addToList(update_args, args[i-1].node());
                contents->rhs.child(args[i-1]);
            }                                                            

            contents->rhs.child(r);
           
            MLVal reduction_args = makeList();
        const RDom &rdom = contents->rhs.rdom();
            for (int i = rdom.dimensions(); i > 0; i--) {
                reduction_args = addToList(reduction_args, 
                                           makeTriple(rdom[i-1].name(), 
                                                      rdom[i-1].min().node(), 
                                                      rdom[i-1].size().node()));
            }

            // Make an update function as a handle for scheduling
            contents->update.reset(new Func(uniqueName('p')));
            
            //printf("Adding scatter definition for %s\n", name().c_str());
            // There should already be a gathering definition of this function. Add the scattering term.
            *environment = addScatterToDefinition(*environment, name(), contents->update->name(), 
                                                  update_args, r.node(), reduction_args);


        }
    }

    Func &Func::update() {
        assert(contents->update);
        return *contents->update;
    }

    void *watchdog(void *arg) {
        useconds_t t = ((useconds_t *)arg)[0];
        printf("Watchdog sleeping for %d microseconds\n", t);
        usleep(t);
        printf("Took too long, bailing out\n");
        exit(-1);
    }

    int Func::autotune(int argc, char **argv, std::vector<int> sizes) {
        timeval before, after;

        printf("sizes: ");
        for (size_t i = 0; i < sizes.size(); i++) {
            printf("%u ", sizes[i]);
        }
        printf("\n");

        if (argc == 1) {
            // Run with default schedule to establish baseline timing (including basic compilation time)
            gettimeofday(&before, NULL);
            DynImage im = realize(sizes);
            for (int i = 0; i < 5; i++) realize(im);
            gettimeofday(&after, NULL);            
            useconds_t t = (after.tv_sec - before.tv_sec) * 1000000 + (after.tv_usec - before.tv_usec);
            printf("%d\n", t);
            return 0;
        }        

        // How to schedule it
        for (int i = 2; i < argc; i++) {
            // One random transform per function per arg
            srand(atoi(argv[i]));            
            for (const Func &_f : rhs().funcs()) {
                Func f = _f;
                f.random(rand());
            }
            random(rand());
        }        

        // Set up a watchdog to kill us if we take too long 
        printf("Setting up watchdog timer\n");
        useconds_t time_limit = atoi(argv[1]) + 1000000;
        pthread_t watchdog_thread;
        pthread_create(&watchdog_thread, NULL, watchdog, &time_limit);

        // Trigger compilation and one round of evaluation
        DynImage im = realize(sizes);

        // Start the clock
        gettimeofday(&before, NULL);
        for (int i = 0; i < 5; i++) realize(im);
        gettimeofday(&after, NULL);            
        useconds_t t = (after.tv_sec - before.tv_sec) * 1000000 + (after.tv_usec - before.tv_usec);
        printf("%d\n", t);        
        return 0;
    }

    Func &Func::tile(const Var &x, const Var &y, 
                     const Var &xi, const Var &yi, 
                     const Expr &f1, const Expr &f2) {
        split(x, x, xi, f1);
        split(y, y, yi, f2);
        transpose(x, yi);
        return *this;
    }

    Func &Func::tile(const Var &x, const Var &y, 
                     const Var &xo, const Var &yo,
                     const Var &xi, const Var &yi, 
                     const Expr &f1, const Expr &f2) {
        split(x, xo, xi, f1);
        split(y, yo, yi, f2);
        transpose(xo, yi);
        return *this;
    }

    Func &Func::vectorize(const Var &v) {
        MLVal t = makeVectorizeTransform((name()),
                                         (v.name()));
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::vectorize(const Var &v, int factor) {
        if (factor == 1) return *this;
        Var vi;
        split(v, v, vi, factor);
        vectorize(vi);        
        return *this;
    }

    Func &Func::unroll(const Var &v) {
        MLVal t = makeUnrollTransform((name()),
                                      (v.name()));        
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::unroll(const Var &v, int factor) {
        if (factor == 1) return *this;
        Var vi;
        split(v, v, vi, factor);
        unroll(vi);
        return *this;
    }

    Func &Func::split(const Var &old, const Var &newout, const Var &newin, const Expr &factor) {
        MLVal t = makeSplitTransform(name(),
                                     old.name(),
                                     newout.name(),
                                     newin.name(),
                                     factor.node());
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::transpose(const Var &outer, const Var &inner) {
        MLVal t = makeTransposeTransform((name()),
                                         (outer.name()),
                                         (inner.name()));
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::chunk(const Var &caller_var) {
        MLVal t = makeChunkTransform(name(), caller_var.name());
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::root() {
        MLVal t = makeRootTransform(name());
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::random(int seed) {
        MLVal t = makeRandomTransform(name(), seed);
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::reset() {
        contents->scheduleTransforms.clear();
        return *this;
    }

    Func &Func::parallel(const Var &caller_var) {
        MLVal t = makeParallelTransform(name(), caller_var.name());
        contents->scheduleTransforms.push_back(t);
        return *this;
    }

    Func &Func::rename(const Var &oldname, const Var &newname) {
        Var dummy;
        return split(oldname, newname, dummy, 1);
    }

    Func &Func::cuda(const Var &b, const Var &t) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        rename(b, bidx);
        rename(t, tidx);
        parallel(bidx);
        parallel(tidx);
        return *this;
    }

    Func &Func::cuda(const Var &bx, const Var &by, 
                     const Var &tx, const Var &ty) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var tidy("threadidy");
        Var bidy("blockidy");
        rename(bx, bidx);
        rename(tx, tidx);
        rename(by, bidy);
        rename(ty, tidy);
        parallel(bidx);
        parallel(bidy);
        parallel(tidx);
        parallel(tidy);
        return *this;
    }


    Func &Func::cudaTile(const Var &x, int xFactor) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        split(x, bidx, tidx, xFactor);
        parallel(bidx);
        parallel(tidx);
        return *this;
    }

    Func &Func::cudaTile(const Var &x, const Var &y, int xFactor, int yFactor) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var tidy("threadidy");
        Var bidy("blockidy");
        tile(x, y, bidx, bidy, tidx, tidy, xFactor, yFactor);
        parallel(bidx);
        parallel(tidx);
        parallel(bidy);
        parallel(tidy);
        return *this;
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

    DynImage Func::realize(std::vector<int> sizes) {
        DynImage im(returnType(), sizes);
        realize(im);
        return im;
    }


    MLVal Func::Contents::applyScheduleTransforms(MLVal guru) {
        // If we're not inline, obey any tuple shape scheduling hints
        if (scheduleTransforms.size() && rhs.isDefined() && rhs.shape().size()) {
            /*
            printf("%s has tuple shape: ", name.c_str());
            for (size_t i = 0; i < rhs.shape().size(); i++) {
                printf("%d ", rhs.shape()[i]);
            }
            printf("\n");
            */

            for (size_t i = 0; i < rhs.shape().size(); i++) {
                assert(args[args.size()-1-i].isVar());
                // The tuple var is the first implicit var (TODO: this is very very ugly)
                Var t("iv0");
                // Pull all the vars inside the tuple var outside
                bool inside = false;
                for (size_t j = args.size(); j > 0; j--) {
                    assert(args[j-1].isVar());
                    Var x = args[j-1].vars()[0];
                    if (x.name() == t.name()) {
                        inside = true;
                        continue;
                    }
                    if (inside) {
                        //printf("Pulling %s outside of %s\n", x.name().c_str(), t.name().c_str());
                        MLVal trans = makeTransposeTransform(name, x.name(), t.name());
                        guru = trans(guru);
                    }
                }
                assert(inside);

                MLVal trans = makeBoundTransform(name, t.name(), Expr(0).node(), Expr(rhs.shape()[i]).node());
                guru = trans(guru);
                trans = makeUnrollTransform(name, t.name());
                guru = trans(guru);
            }
        }
        for (size_t i = 0; i < scheduleTransforms.size(); i++) {
            guru = scheduleTransforms[i](guru);
        }
        if (update) {
            guru = update->contents->applyScheduleTransforms(guru);
        }
        return guru;
    }

    // Returns a stmt, args pair
    MLVal Func::lower() {
        // Make a region to evaluate this over
        MLVal sizes = makeList();        
        for (size_t i = args().size(); i > 0; i--) {                
            char buf[256];
            snprintf(buf, 256, ".result.dim.%d", ((int)i)-1);
            sizes = addToList(sizes, Expr(Var(buf)).node());
        }

        MLVal guru = makeNoviceGuru();

        // Output is always scheduled root
        root();

        guru = contents->applyScheduleTransforms(guru);

        for (size_t i = 0; i < rhs().funcs().size(); i++) {
            Func f = rhs().funcs()[i];
            // Don't consider recursive dependencies for the
            // purpose of applying schedule transformations. We
            // already did that above.
            if (f == *this) continue;
            guru = f.contents->applyScheduleTransforms(guru);
        }

        //saveGuruToFile(guru, name() + ".guru");
        
        MLVal sched = makeSchedule((name()),
                                   sizes,
                                   *Func::environment,
                                   guru);
        
        //printf("Done transforming schedule\n");
        //printSchedule(sched);
        
        return doLower((name()), 
                       *Func::environment,
                       sched);        
    }

    MLVal Func::inferArguments() {        
        MLVal fargs = makeList();
        fargs = addToList(fargs, makeBufferArg("result"));
        for (size_t i = rhs().uniformImages().size(); i > 0; i--) {
            MLVal arg = makeBufferArg(rhs().uniformImages()[i-1].name());
            fargs = addToList(fargs, arg);
        }
        for (size_t i = rhs().images().size(); i > 0; i--) {
            MLVal arg = makeBufferArg(rhs().images()[i-1].name());
            fargs = addToList(fargs, arg);
        }
        for (size_t i = rhs().uniforms().size(); i > 0; i--) {
            const DynUniform &u = rhs().uniforms()[i-1];
            MLVal arg = makeScalarArg(u.name(), u.type().mlval);
            fargs = addToList(fargs, arg);
        }
        return fargs;
    }

    Func::Arg::Arg(const UniformImage &u) : arg(makeBufferArg(u.name())) {}
    Func::Arg::Arg(const DynUniform &u) : arg(makeScalarArg(u.name(), u.type().mlval)) {}
    Func::Arg::Arg(const DynImage &u) : arg(makeBufferArg(u.name())) {}

    std::string Func::serialize() {
        MLVal stmt = lower();
        MLVal args = inferArguments();
        return std::string(serializeEntry(name(), args, stmt));
    }

    void Func::compileToFile(const std::string &moduleName) { 
        MLVal stmt = lower();
        MLVal args = inferArguments();
        doCompileToFile(moduleName, args, stmt);
    }

    void Func::compileToFile(const std::string &moduleName, std::vector<Func::Arg> uniforms) { 
        MLVal stmt = lower();

        MLVal args = makeList();
        args = addToList(args, makeBufferArg("result"));
        for (size_t i = uniforms.size(); i > 0; i--) {
            args = addToList(args, uniforms[i-1].arg);
        }

        doCompileToFile(moduleName, args, stmt);
    }

    void Func::setErrorHandler(void (*handler)(char *)) {
        contents->errorHandler = handler;
    }

    void Func::compileJIT() {

        // If JITting doesn't work well on this platform (ARM), try
        // compiling to a shared lib instead and manually linking it
        // in. Also useful for debugging.
        if (getenv("HL_PSEUDOJIT") && getenv("HL_PSEUDOJIT") == std::string("1")) {
            fprintf(stderr, "Pseudo-jitting via static compilation to a shared object\n");
            
            std::string name = contents->name + "_pseudojit";
            std::string so_name = "./" + name + ".so";
            std::string entrypoint_name = name + "_c_wrapper";
            
            // Compile the object, unless HL_PSEUDOJIT_LOAD_PRECOMPILED is set
            if (!getenv("HL_PSEUDOJIT_LOAD_PRECOMPILED")) {
                compileToFile(name.c_str());
                char cmd1[1024], cmd2[1024];

                std::string obj_name = "./" + name + ".o";
                if (getenv("HL_BACKEND") && getenv("HL_BACKEND") == std::string("c")) {
                    std::string c_name = "./" + name + ".c";
                    snprintf(cmd1, 1024, "g++ -c -O3 %s -fPIC -o %s", c_name.c_str(), obj_name.c_str());
                } else {
                    std::string bc_name = "./" + name + ".bc";
                    snprintf(cmd1, 1024, 
			     "opt -O3 -always-inline %s | llc -O3 -relocation-model=pic %s -filetype=obj > %s", 
			     bc_name.c_str(), use_avx() ? "-mcpu=corei7 -mattr=+avx" : "", obj_name.c_str());
                }
                snprintf(cmd2, 1024, "gcc -shared %s -o %s", obj_name.c_str(), so_name.c_str());
                fprintf(stderr, "%s\n", cmd1);
                assert(0 == system(cmd1));
                fprintf(stderr, "%s\n", cmd2);
                assert(0 == system(cmd2));
            }
            void *handle = dlopen(so_name.c_str(), RTLD_LAZY);
            fprintf(stderr, "dlopen(%s)\n", so_name.c_str());
            if (!handle) perror("dlopen");
            assert(handle && "Could not open shared object file when pseudojitting");
            void *ptr = dlsym(handle, entrypoint_name.c_str());
            assert(ptr && "Could not find entrypoint in shared object file when pseudojitting");
            contents->functionPtr = (void (*)(void *))ptr;

            // Hook up any custom error handler
            if (contents->errorHandler) {
                ptr = dlsym(handle, "set_error_handler");
                assert(ptr && "Could not find set_error_handler in shared object file when pseudojitting");
                void (*setErrorHandlerFn)(void (*)(char *)) = (void (*)(void (*)(char *)))ptr;
                setErrorHandlerFn(contents->errorHandler);
            }
            
            return;
        }

        if (!Contents::ee) {
            llvm::InitializeNativeTarget();
	    llvm::InitializeNativeTargetAsmPrinter();
        }

        // Use the function definitions and the schedule to create the
        // blob of imperative IR
        MLVal stmt = lower();
        
        // Hook up uniforms, images, etc and turn them into the
        // argument list for the llvm function
        MLVal args = inferArguments();

        // Create the llvm module and entrypoint from the imperative IR
        MLVal tuple;
        tuple = doCompile(name(), args, stmt);

        // Extract the llvm module and entrypoint function
        MLVal first, second;
        MLVal::unpackPair(tuple, first, second);
        LLVMModuleRef module = (LLVMModuleRef)(first.asVoidPtr());
        LLVMValueRef func = (LLVMValueRef)(second.asVoidPtr());
        llvm::Function *f = llvm::unwrap<llvm::Function>(func);
        llvm::Module *m = llvm::unwrap(module);

        // Create the execution engine if it hasn't already been done
        if (!Contents::ee) {
            std::string errStr;
            llvm::EngineBuilder eeBuilder(m);
            eeBuilder.setErrorStr(&errStr);
	    eeBuilder.setEngineKind(llvm::EngineKind::JIT);
	    eeBuilder.setUseMCJIT(false);
            eeBuilder.setOptLevel(llvm::CodeGenOpt::Aggressive);

            // runtime-detect avx to only enable it if supported
	    // disabled for now until we upgrade llvm
	    if (use_avx() && 0) {
		std::vector<std::string> mattrs = {"avx"};
		eeBuilder.setMAttrs(mattrs);
	    }

            Contents::ee = eeBuilder.create();
            if (!contents->ee) {
                printf("Couldn't create execution engine: %s\n", errStr.c_str()); 
                exit(1);
            }

            // Set up the pass manager
            Contents::fPassMgr = new llvm::FunctionPassManager(m);
            Contents::mPassMgr = new llvm::PassManager();

            // Make sure to include the always-inliner pass so that
            // unaligned_load and other similar one-opcode functions
            // always get inlined.
            Contents::mPassMgr->add(llvm::createAlwaysInlinerPass());

            // Add every other pass used by -O3
            llvm::PassManagerBuilder builder;
            builder.OptLevel = 3;
            builder.populateFunctionPassManager(*contents->fPassMgr);
            builder.populateModulePassManager(*contents->mPassMgr);

        } else { 
            // Execution engine is already created. Add this module to it.
            Contents::ee->addModule(m);
        }            
        
        std::string functionName = name() + "_c_wrapper";
        llvm::Function *inner = m->getFunction(functionName.c_str());
        
        if (use_gpu()) {
            // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
            // CUDA behaves much better when you don't initialize >2 contexts.
            llvm::GlobalVariable* ctx = m->getNamedGlobal("cuda_ctx");
            if (ctx) {
                Contents::ee->addGlobalMapping(ctx, (void*)&cuda_ctx);
            }        

            // Make sure extern cuda calls inside the module point to
            // the right things. This is done manually instead of
            // relying on llvm calling dlsym because that solution
            // doesn't seem to work on linux with cuda 4.2. It also
            // means that if the user forgets to link to libcuda at
            // compile time then this code will go look for it.
            if (!Contents::libCuda && !Contents::libCudaLinked) {
                // First check if libCuda has already been linked
                // in. If so we shouldn't need to set any mappings.
                if (dlsym(NULL, "cuInit")) {
                    // TODO: Andrew: This code path not tested yet,
                    // because I can't get linking to libcuda working
                    // right on my machine.
                    fprintf(stderr, "This program was linked to libcuda already\n");
                    Contents::libCudaLinked = true;
                } else {
                    fprintf(stderr, "Looking for libcuda.so...\n");
                    Contents::libCuda = dlopen("libcuda.so", RTLD_LAZY);
                    if (!Contents::libCuda) {
                        // TODO: check this works on OS X
                        fprintf(stderr, "Looking for libcuda.dylib...\n");
                        Contents::libCuda = dlopen("libcuda.dylib", RTLD_LAZY);
                    }
                    // TODO: look for cuda.dll or some such thing on windows
                }
            }
            
            if (!Contents::libCuda && !Contents::libCudaLinked) {
                fprintf(stderr, 
                        "Error opening libcuda. Attempting to continue anyway."
                        "Might get missing symbols.\n");
            } else if (Contents::libCudaLinked) {
                // Shouldn't need to do anything. llvm will call dlsym
                // on the current process for us.
            } else {
                for (auto f = m->begin(); f != m->end(); f++) {
                    llvm::StringRef name = f->getName();
                    if (f->hasExternalLinkage() && name[0] == 'c' && name[1] == 'u') {
                        // Starts with "cu" and has extern linkage. Might be a cuda function.
                        fprintf(stderr, "Linking %s\n", name.str().c_str());
                        void *ptr = dlsym(Contents::libCuda, name.str().c_str());
                        if (ptr) Contents::ee->updateGlobalMapping(f, ptr);
                    }
                }
            }
        }
        
        assert(inner && "Could not find c wrapper inside llvm module");
        
        // Run optimization passes

        // Turning on this code will dump the result of all the optimization passes to a file
        // std::string errstr;
        // llvm::raw_fd_ostream stdout("passes.txt", errstr);

        Contents::mPassMgr->run(*m);
        Contents::fPassMgr->doInitialization();       
        Contents::fPassMgr->run(*inner);        
        Contents::fPassMgr->doFinalization();

        void *ptr = Contents::ee->getPointerToFunction(f);
        contents->functionPtr = (void (*)(void*))ptr;

        // Retrieve some functions inside the module that we'll want to call from C++
        llvm::Function *copyToHost = m->getFunction("__copy_to_host");
        if (copyToHost) {
            ptr = Contents::ee->getPointerToFunction(copyToHost);
            contents->copyToHost = (void (*)(buffer_t*))ptr;
        }
        
        llvm::Function *freeBuffer = m->getFunction("__free_buffer");
        if (freeBuffer) {
            ptr = Contents::ee->getPointerToFunction(freeBuffer);
            contents->freeBuffer = (void (*)(buffer_t*))ptr;
        }       

        // If we have a custom error handler, hook it up here
        if (contents->errorHandler) {
            llvm::Function *setErrorHandler = m->getFunction("set_error_handler");
            assert(setErrorHandler && 
                   "Could not find the set_error_handler function in the compiled module\n");
            ptr = Contents::ee->getPointerToFunction(setErrorHandler);
            void (*setErrorHandlerFn)(void (*)(char *)) = (void (*)(void (*)(char *)))ptr;
            setErrorHandlerFn(contents->errorHandler);
        }
    }

    size_t im_size(const DynImage &im, int dim) {
        return im.size(dim);
    }
    
    size_t im_size(const UniformImage &im, int dim) {
        return im.boundImage().size(dim);
    }

    void Func::realize(const DynImage &im) {
        if (!contents->functionPtr) compileJIT();

        //printf("Constructing argument list...\n");
        void *arguments[256];
        buffer_t *buffers[256];
        size_t j = 0;
        size_t k = 0;

        for (size_t i = 0; i < rhs().uniforms().size(); i++) {
            arguments[j++] = rhs().uniforms()[i].data();
        }
        for (size_t i = 0; i < rhs().images().size(); i++) {
            buffers[k++] = rhs().images()[i].buffer();
            arguments[j++] = buffers[k-1];
        }               
        for (size_t i = 0; i < rhs().uniformImages().size(); i++) {
            buffers[k++] = rhs().uniformImages()[i].boundImage().buffer();
            arguments[j++] = buffers[k-1];
        }
        buffers[k] = im.buffer();
        arguments[j] = buffers[k];

        /*
        printf("Args: ");
        for (size_t i = 0; i <= j; i++) {
            printf("%p ", arguments[i]);
        }
        printf("\n");

        printf("Calling function at %p\n", contents->functionPtr); 
        */
        contents->functionPtr(&arguments[0]);
        
        if (use_gpu()) {
            assert(contents->copyToHost);
            im.setRuntimeHooks(contents->copyToHost, contents->freeBuffer);
        }
        
        // TODO: the actual codegen entrypoint should probably set this for x86/ARM targets too
        if (!im.devDirty()) {
            im.markHostDirty();
        }
    }

    MLVal *Func::environment = NULL;

}

