#include "RDom.h"
#include "Util.h"

namespace Halide {

RVar::operator Expr() const {
    return new Variable(Int(32), name(), domain);
}

Internal::ReductionDomain build_domain(string name0, Expr min0, Expr extent0, 
                                       string name1, Expr min1, Expr extent1, 
                                       string name2, Expr min2, Expr extent2, 
                                       string name3, Expr min3, Expr extent3) {
    vector<Internal::ReductionVariable> d;
    if (min0.defined()) {
        Internal::ReductionVariable v = {name0, min0, extent0};
        d.push_back(v);
    }
    if (min1.defined()) {
        Internal::ReductionVariable v = {name1, min1, extent1};
        d.push_back(v);
    }
    if (min2.defined()) {
        Internal::ReductionVariable v = {name2, min2, extent2};
        d.push_back(v);
    }
    if (min3.defined()) {
        Internal::ReductionVariable v = {name3, min3, extent3};
        d.push_back(v);
    }

    Internal::ReductionDomain dom(d);
    
    return dom;
}

RDom::RDom(Expr min, Expr extent, string name) {
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min, extent, 
                          "", Expr(), Expr(), 
                          "", Expr(), Expr(), 
                          "", Expr(), Expr());
    x = RVar(name + ".x", min, extent, domain);
}

RDom::RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, string name) {
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min0, extent0, 
                          name + ".y", min1, extent1,
                          "", Expr(), Expr(), 
                          "", Expr(), Expr());
    x = RVar(name + ".x", min0, extent0, domain);
    y = RVar(name + ".y", min1, extent1, domain);
}

RDom::RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, Expr min2, Expr extent2, string name) {
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min0, extent0, 
                          name + ".y", min1, extent1,
                          name + ".z", min2, extent2,
                          "", Expr(), Expr());
    x = RVar(name + ".x", min0, extent1, domain);
    y = RVar(name + ".y", min1, extent1, domain);
    z = RVar(name + ".z", min2, extent2, domain);
}

RDom::RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, Expr min2, Expr extent2, Expr min3, Expr extent3, string name) {
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min0, extent0, 
                          name + ".y", min1, extent1, 
                          name + ".z", min2, extent2, 
                          name + ".w", min3, extent3);
    x = RVar(name + ".x", min0, extent1, domain);
    y = RVar(name + ".y", min1, extent1, domain);
    z = RVar(name + ".z", min2, extent2, domain);
    w = RVar(name + ".w", min3, extent3, domain);
}

RDom::RDom(Buffer b) {   
    domain = build_domain(b.name() + ".x", b.min(0), b.extent(0),
                          b.name() + ".y", b.min(1), b.extent(1),
                          b.name() + ".z", b.min(2), b.extent(2), 
                          b.name() + ".w", b.min(3), b.extent(3));
    x = RVar(b.name() + ".x", b.min(0), b.extent(1), domain);
    y = RVar(b.name() + ".y", b.min(1), b.extent(1), domain);
    z = RVar(b.name() + ".z", b.min(2), b.extent(2), domain);
    w = RVar(b.name() + ".w", b.min(3), b.extent(3), domain);
}

RDom::RDom(ImageParam p) {
    domain = build_domain(p.name() + ".x", 0, p.extent(0),
                          p.name() + ".y", 0, p.extent(1),
                          p.name() + ".z", 0, p.extent(2), 
                          p.name() + ".w", 0, p.extent(3));
    x = RVar(p.name() + ".x", 0, p.extent(1), domain);
    y = RVar(p.name() + ".y", 0, p.extent(1), domain);
    z = RVar(p.name() + ".z", 0, p.extent(2), domain);
    w = RVar(p.name() + ".w", 0, p.extent(3), domain);
}


int RDom::dimensions() const {
    return domain.domain().size();
}

RVar RDom::operator[](int i) {
    if (i == 0) return x;
    if (i == 1) return y;
    if (i == 2) return z;
    if (i == 3) return w;
    assert(false && "Reduction domain index out of bounds");
}

RDom::operator Expr() const {
    assert(dimensions() == 1 && "Can only treat single-dimensional RDoms as expressions");
    return Expr(x);
}

}
