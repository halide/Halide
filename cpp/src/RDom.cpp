#include "RDom.h"
#include "Util.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {

using std::string;
using std::vector;

RVar::operator Expr() const {
    return new Internal::Variable(Int(32), name(), domain);
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
    min = cast<int>(min);
    extent = cast<int>(extent);
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min, extent, 
                          "", Expr(), Expr(), 
                          "", Expr(), Expr(), 
                          "", Expr(), Expr());
    x = RVar(name + ".x", min, extent, domain);
}

RDom::RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, string name) {
    min0 = cast<int>(min0);
    extent0 = cast<int>(extent0);
    min1 = cast<int>(min1);
    extent1 = cast<int>(extent1);
    if (name == "") name = Internal::unique_name('r');
    domain = build_domain(name + ".x", min0, extent0, 
                          name + ".y", min1, extent1,
                          "", Expr(), Expr(), 
                          "", Expr(), Expr());
    x = RVar(name + ".x", min0, extent0, domain);
    y = RVar(name + ".y", min1, extent1, domain);
}

RDom::RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, Expr min2, Expr extent2, string name) {
    min0 = cast<int>(min0);
    extent0 = cast<int>(extent0);
    min1 = cast<int>(min1);
    extent1 = cast<int>(extent1);
    min2 = cast<int>(min2);
    extent2 = cast<int>(extent2);
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
    min0 = cast<int>(min0);
    extent0 = cast<int>(extent0);
    min1 = cast<int>(min1);
    extent1 = cast<int>(extent1);
    min2 = cast<int>(min2);
    extent2 = cast<int>(extent2);
    min3 = cast<int>(min3);
    extent3 = cast<int>(extent3);
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
    Expr min[4], extent[4];
    for (int i = 0; i < 4; i++) {
        if (b.dimensions() > i) {
            min[i] = b.min(i);
            extent[i] = b.extent(i);
        }    
    }
    string names[] = {b.name() + ".x", b.name() + ".y", b.name() + ".z", b.name() + ".w"};
    domain = build_domain(names[0], min[0], extent[0],
                          names[1], min[1], extent[1],
                          names[2], min[2], extent[2],
                          names[3], min[3], extent[3]);    
    RVar *vars[] = {&x, &y, &z, &w};
    for (int i = 0; i < 4; i++) {
        if (b.dimensions() > i) {
            *(vars[i]) = RVar(names[i], min[i], extent[i], domain);
        }
    }
}

RDom::RDom(ImageParam p) {
    Expr min[4], extent[4];
    for (int i = 0; i < 4; i++) {
        if (p.dimensions() > i) {
            min[i] = 0;
            extent[i] = p.extent(i);
        }    
    }
    string names[] = {p.name() + ".x", p.name() + ".y", p.name() + ".z", p.name() + ".w"};
    domain = build_domain(names[0], min[0], extent[0],
                          names[1], min[1], extent[1],
                          names[2], min[2], extent[2],
                          names[3], min[3], extent[3]);    
    RVar *vars[] = {&x, &y, &z, &w};
    for (int i = 0; i < 4; i++) {
        if (p.dimensions() > i) {
            *(vars[i]) = RVar(names[i], min[i], extent[i], domain);
        }
    }
}


int RDom::dimensions() const {
    return (int)domain.domain().size();
}

RVar RDom::operator[](int i) {
    if (i == 0) return x;
    if (i == 1) return y;
    if (i == 2) return z;
    if (i == 3) return w;
    assert(false && "Reduction domain index out of bounds");
	return x; // Keep the compiler happy
}

RDom::operator Expr() const {
    assert(dimensions() == 1 && "Can only treat single-dimensional RDoms as expressions");
    return Expr(x);
}

RDom::operator RVar() const {
    assert(dimensions() == 1 && "Can only treat single-dimensional RDoms as RVars");
    return x;
}

}
