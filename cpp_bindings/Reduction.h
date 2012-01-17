#ifndef HALIDE_REDUCTION_H
#define HALIDE_REDUCTION_H

#include "Expr.h"
#include "Var.h"
#include "Func.h"
#include <assert.h>
#include <vector>
#include <memory>

namespace Halide {
  
    // Reductions are anonymous functions that can be cast to an Expr to use
    class sum {
    public:
        sum(const Expr &body) {
            Func anon;
            std::vector<Expr> args(body.vars().size());
            for (size_t i = 0; i < body.vars().size(); i++) {
                args[i] = body.vars()[i];
            }
            Expr init = cast(body.type(), 0);
            init.addImplicitArgs(body.implicitArgs());
            anon(args) = init;
            anon(args) = anon(args) + body;
            call = anon(args);
        }

        operator Expr() {
            return call;
        }
    private:
        Expr call;
    };

    class product {
    public:
        product(const Expr &body) {
            Func anon;
            std::vector<Expr> args(body.vars().size());
            for (size_t i = 0; i < body.vars().size(); i++) {
                args[i] = body.vars()[i];
            }
            Expr init = cast(body.type(), 1);
            init.addImplicitArgs(body.implicitArgs());
            anon(args) = init;
            anon(args) = anon(args) * body;
            call = anon(args);
        }
        
        operator Expr() {
            return call;
        }
    private:
        Expr call;
    };
}

#endif

