#ifndef HALIDE_TUPLE_H
#define HALIDE_TUPLE_H

#include <assert.h>
#include <vector>
#include "Expr.h"

namespace Halide {

    class Tuple {
    public:
        Tuple(const Expr &a) : contents{a} {}

        Tuple(const Expr &a, const Expr &b) : contents{a, b} {}

        Tuple(const Expr &a, const Expr &b, const Expr &c) : contents{a, b, c} {}

        Tuple(const Expr &a, const Expr &b, const Expr &c, const Expr &d) : contents{a, b, c, d} {}

        Tuple operator,(const Expr &e) const {
            return Tuple(contents, e);
        }

        Tuple operator,(const Tuple &other) const {
            assert(contents.size() == other.contents.size());
            return Tuple(Expr(*this), Expr(other));
        }

        operator Expr() const; 
        
    private:
        Tuple(const std::vector<Expr> &init, const Expr &last) : contents(init) {
            contents.push_back(last);
        }

        std::vector<Expr> contents;
    };

    Tuple operator,(const Expr & a, const Expr &b);
   
}

#endif
