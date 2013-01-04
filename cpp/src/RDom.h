#ifndef HALIDE_RDOM_H
#define HALIDE_RDOM_H

#include "IR.h"
#include "Param.h"

namespace Halide {

// This is the front-end handle to a reduction domain
class RVar {
    string _name;
    Expr _min, _extent;
    Internal::ReductionDomain domain;
public:
    RVar() {}
    RVar(string name, Expr m, Expr e, Internal::ReductionDomain d) : _name(name), _min(m), _extent(e), domain(d) {}
    Expr min() const {return _min;}
    Expr extent() const {return _extent;}
    const string &name() const {return _name;}

    operator Expr() const;
};

class RDom {
    Internal::ReductionDomain domain;
public:
    RDom(Expr min, Expr extent, string name = "");
    RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, string name = "");
    RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, Expr min2, Expr extent2, string name = "");
    RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, Expr min2, Expr extent2, Expr min3, Expr extent3, string name = "");
    RDom(Buffer);
    RDom(ImageParam);

    bool defined() const {return domain.defined();}
    bool same_as(const RDom &other) const {return domain.same_as(other.domain);}

    int dimensions() const;
    RVar operator[](int);
    operator Expr() const;

    RVar x, y, z, w;
};

// Some inline reductions. They expect the expression argument to
// refer to some reduction domain. They may contain free variables.
Expr sum(Expr);
Expr product(Expr);
Expr maximum(Expr);
Expr minimum(Expr);

}

#endif
