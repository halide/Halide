#include "FuncContents.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
    ML_FUNC0(makeIdentity);
    ML_FUNC3(makeDefinition);
    ML_FUNC6(addScatterToDefinition);
    ML_FUNC2(addDefinitionToEnv);

    ML_FUNC1(functionIsPure);
    ML_FUNC1(functionIsReduce);

    ML_FUNC1(getPureBody);
    ML_FUNC1(getReduceBody);

    LLVMExecutionEngineRef FuncContents::ee = NULL;
    LLVMPassManagerRef FuncContents::fPassMgr = NULL;
    LLVMPassManagerRef FuncContents::mPassMgr = NULL;
    void *FuncContents::libCuda = NULL;
    bool FuncContents::libCudaLinked = false;

    MLVal FuncContents::applyGuru(MLVal g) {
        g = guru(g);
        if (update) g = update->contents->guru(g);
        return g;
    }

    MLVal FuncContents::addDefinition(MLVal env) {
        MLVal arglist = makeList();
        for (size_t i = args.size(); i > 0; i--) {
            arglist = addToList(arglist, args[i-1].vars()[0].name());
        }
        MLVal definition = makeDefinition(name, arglist, rhs.node());           
        env = addDefinitionToEnv(env, definition);
        
        if (update) {
            MLVal update_args = makeList();
            RDom rdom;
            for (size_t i = update->contents->args.size(); i > 0; i--) {
                const Expr &arg = update->contents->args[i-1];
                update_args = addToList(update_args, arg.node());
                if (arg.rdom().dimensions()) rdom = arg.rdom();
            }                                                            
            
            MLVal reduction_domain = makeList();
            
            const Expr &rhs = update->contents->rhs;
            if (rhs.rdom().dimensions()) rdom = rhs.rdom();
            
            assert(rdom.dimensions() && "Couldn't find reduction domain in reduction definition");
            
            for (int i = rdom.dimensions(); i > 0; i--) {
                reduction_domain = addToList(reduction_domain, 
                                             makeTriple(rdom[i-1].name(), 
                                                        rdom[i-1].min().node(), 
                                                        rdom[i-1].size().node()));
            }
            
            env = addScatterToDefinition(env, name, update->name(), 
                                         update_args, rhs.node(), reduction_domain);            
        }
        return env;
    }
}
