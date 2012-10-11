#include "ExprContents.h"
#include "Util.h"

namespace Halide {
    ML_FUNC1(makeVar);
    ML_FUNC3(makeFuncCall);
    
    // declare that this node has a child for bookkeeping
    void ExprContents::child(Expr c) {
        set_union(images, c.images());
        set_union(vars, c.vars());
        set_union(funcs, c.funcs());
        set_union(uniforms, c.uniforms());
        set_union(uniformImages, c.uniformImages());
        if (c.implicitArgs() > implicitArgs) implicitArgs = c.implicitArgs();

        bool check = !rdom.isDefined() || !c.rdom().isDefined() || rdom == c.rdom();
        assert(check && "Each expression can only depend on a single reduction domain");
        if (c.rdom().isDefined()) {
            rdom = c.rdom();
        }        
    }

    ExprContents::ExprContents(const FuncRef &f) {
        assert(f.f().rhs().isDefined() && 
               "Can't use a call to an undefined function as an expression\n");

        // make a call node
        MLVal exprlist = makeList();

        // Start with the implicit arguments
        /*printf("This call to %s has %d arguments when %s takes %d args\n", 
               f.f().name().c_str(),
               (int)f.args().size(),
               f.f().name().c_str(),
               (int)f.f().args().size()); */
        int iArgs = (int)f.f().args().size() - (int)f.args().size();
        if (iArgs < 0 && f.f().args().size() > 0) {
            printf("Too many arguments in call!\n");
            exit(-1);
        } 

        for (int i = iArgs-1; i >= 0; i--) {
            exprlist = addToList(exprlist, makeVar(std::string("iv") + int_to_str(i)));  // implicit var. 
        }

        for (size_t i = f.args().size(); i > 0; i--) {
            exprlist = addToList(exprlist, f.args()[i-1].node());            
        }

        node = makeFuncCall(f.f().returnType().mlval, 
                            (f.f().name()),
                            exprlist);
        type = f.f().returnType();

        for (size_t i = 0; i < f.args().size(); i++) {
            if (f.args()[i].implicitArgs() != 0) {
                printf("Can't use a partially applied function as an argument. We don't support higher-order functions.\n");
                exit(-1);
            }
            child(f.args()[i]);
        }

        implicitArgs = iArgs;
        
        // Add this function call to the calls list
        funcs.push_back(f.f());  

        // Reach through the call to extract buffer dependencies and
        // function dependencies (but not free vars, or implicit args)
        if (f.f().rhs().isDefined()) {
            set_union(images, f.f().images());
            set_union(funcs, f.f().funcs());
            set_union(uniforms, f.f().uniforms());
            set_union(uniformImages, f.f().uniformImages());
        }
    }
}