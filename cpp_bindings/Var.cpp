#include "Var.h"
#include "Expr.h"
#include "Image.h"
#include <assert.h>

namespace Halide {

    struct RVar::Contents {
        Contents(RDom dom, Expr min, Expr size, const std::string &name) : 
            min(min), size(size), name(name), domain(dom) {
            assert(min.type() == TypeOf<int>() &&
                   size.type() == TypeOf<int>() &&
                   "Bounds of reduction domain must be integers");
        }
        Expr min, size;
        std::string name;
        RDom domain;
    };
    
    RVar::RVar() {}

    RVar::RVar(const RDom &dom, const Expr &min, const Expr &size) : 
        contents(new Contents(dom, min, size, uniqueName('r'))) {}
    
    RVar::RVar(const RDom &dom, const Expr &min, const Expr &size, const std::string &name) :
        contents(new Contents(dom, min, size, sanitizeName(name))) {}
    
    const Expr &RVar::min() const {
        assert(isDefined() && "RVar is not defined");
        return contents->min;
    }
    
    const Expr &RVar::size() const {
        assert(isDefined() && "RVar is not defined");
        return contents->size;
    }
    
    const std::string &RVar::name() const {
        assert(isDefined() && "RVar is not defined");
        return contents->name;
    }

    const RDom &RVar::domain() const {
        assert(contents->domain.isDefined() && "RVar has no domain!");
        return contents->domain;
    }

    bool RVar::operator==(const RVar &other) const {
        return name() == other.name();
    }

    struct RDom::Contents {
        std::vector<RVar> vars; 
    };

    RDom::RDom() {}

    RDom::RDom(const UniformImage &im) : 
        contents(new RDom::Contents) {
        for (int i = 0; i < im.dimensions(); i++) {
            contents->vars.push_back(RVar(*this, 0, im.size(i), im.name() + "_r" + int_to_str(i))); // Connelly: ostringstream broken in Python binding, use string + instead
        }
        if (im.dimensions() > 0) x = contents->vars[0];
        if (im.dimensions() > 1) y = contents->vars[1];
        if (im.dimensions() > 2) z = contents->vars[2];
        if (im.dimensions() > 3) w = contents->vars[3];
    }

    RDom::RDom(const DynImage &im) : 
        x(), y(), z(), w(), 
        contents(new RDom::Contents) {
        for (int i = 0; i < im.dimensions(); i++) {
            contents->vars.push_back(RVar(*this, 0, im.size(i), im.name() + "_r" + int_to_str(i))); // Connelly: ostringstream broken in Python binding, use string + instead
        }
        if (im.dimensions() > 0) x = contents->vars[0];
        if (im.dimensions() > 1) y = contents->vars[1];
        if (im.dimensions() > 2) z = contents->vars[2];
        if (im.dimensions() > 3) w = contents->vars[3]; 
    }

    RDom::RDom(const Expr &min, const Expr &size) :
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min, size));
        x = contents->vars[0];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1));
        contents->vars.push_back(RVar(*this, min2, size2));
        x = contents->vars[0];
        y = contents->vars[1];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2,
               const Expr &min3, const Expr &size3) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1));
        contents->vars.push_back(RVar(*this, min2, size2));
        contents->vars.push_back(RVar(*this, min3, size3));
        x = contents->vars[0];
        y = contents->vars[1];
        z = contents->vars[2];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2,
               const Expr &min3, const Expr &size3,
               const Expr &min4, const Expr &size4) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1));
        contents->vars.push_back(RVar(*this, min2, size2));
        contents->vars.push_back(RVar(*this, min3, size3));
        contents->vars.push_back(RVar(*this, min4, size4));
        x = contents->vars[0];
        y = contents->vars[1];
        z = contents->vars[2];
        w = contents->vars[3];
    }
    
    RDom::RDom(const Expr &min, const Expr &size, const std::string &name) :
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min, size, name + "_x"));
        x = contents->vars[0];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2,
               const std::string &name) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1, name + "_x"));
        contents->vars.push_back(RVar(*this, min2, size2, name + "_y"));
        x = contents->vars[0];
        y = contents->vars[1];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2,
               const Expr &min3, const Expr &size3,
               const std::string &name) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1, name + "_x"));
        contents->vars.push_back(RVar(*this, min2, size2, name + "_y"));
        contents->vars.push_back(RVar(*this, min3, size3, name + "_z"));
        x = contents->vars[0];
        y = contents->vars[1];
        z = contents->vars[2];
    }

    RDom::RDom(const Expr &min1, const Expr &size1,
               const Expr &min2, const Expr &size2,
               const Expr &min3, const Expr &size3,
               const Expr &min4, const Expr &size4,
               const std::string &name) : 
        contents(new RDom::Contents) {
        contents->vars.push_back(RVar(*this, min1, size1, name + "_x"));
        contents->vars.push_back(RVar(*this, min2, size2, name + "_y"));
        contents->vars.push_back(RVar(*this, min3, size3, name + "_z"));
        contents->vars.push_back(RVar(*this, min4, size4, name + "_w"));
        x = contents->vars[0];
        y = contents->vars[1];
        z = contents->vars[2];
        w = contents->vars[3];
    }

    
    bool RDom::operator==(const RDom &other) const {
        assert(isDefined() && other.isDefined() && "Reduction domain not defined");
        return contents == other.contents;
    }

    const RVar &RDom::operator[](int i) const {
        assert(isDefined() && "Reduction domain not defined\n");
        assert(i >= 0 && i < (int)contents->vars.size() && "Index out of bounds in reduction domain");
        return contents->vars[i];
    }

    int RDom::dimensions() const {
        if (!isDefined()) return 0;
        return contents->vars.size();
    }

}
