#ifndef FIMAGE_REDUCTION_H
#define FIMAGE_REDUCTION_H

#include "Expr.h"
#include "Var.h"
#include "Func.h"
#include <assert.h>
#include <vector>
#include <memory>

namespace FImage {
  
    // Reductions are anonymous functions that can be cast to an Expr to use
    class Sum {
    public:
        Sum(const Expr &body) {
            Func anon;
            std::vector<Expr> args(body.vars().size());
            for (size_t i = 0; i < body.vars().size(); i++) {
                args[i] = body.vars()[i];
            }
            Expr init = Cast(body.type(), 0);
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

    class Product {
    public:
        Product(const Expr &body) {
            Func anon;
            std::vector<Expr> args(body.vars().size());
            for (size_t i = 0; i < body.vars().size(); i++) {
                args[i] = body.vars()[i];
            }
            Expr init = Cast(body.type(), 1);
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

