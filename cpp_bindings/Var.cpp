#include "Var.h"
#include "Expr.h"
#include <assert.h>

namespace Halide {

    struct RVar::Contents {
        Contents(Expr min, Expr size, const std::string &name) : min(min), size(size), name(name) {}
        Expr min, size;
        std::string name;
    };
    
    RVar::RVar(const std::string &name) :
        contents(new Contents(Expr(), Expr(), name)) {}

    RVar::RVar() :
        contents(new Contents(Expr(), Expr(), uniqueName('r'))) {}

    RVar::RVar(const Expr &min, const Expr &size) : 
        contents(new Contents(min, size, uniqueName('r'))) {}
    
    RVar::RVar(const Expr &min, const Expr &size, const std::string &name) :
        contents(new Contents(min, size, name)) {}
    
    void RVar::bound(const Expr &m, const Expr &size) {
        printf("Bounding %s\n", name().c_str());
        if (contents->min.isDefined()) {
            contents->min = max(contents->min, m);
        } else {
            contents->min = m;
        }

        if (contents->size.isDefined()) {
            contents->size = Halide::min(contents->size, size);
        } else {
            contents->size = size;
        }
    }

    const Expr &RVar::min() const {
        assert(contents->min.isDefined());
        return contents->min;
    }
    
    const Expr &RVar::size() const {
        assert(contents->size.isDefined());
        return contents->size;
    }
    
    const std::string &RVar::name() const {
        return contents->name;
    }
    
    bool RVar::operator==(const RVar &other) const {
        return name() == other.name();
    }

}
