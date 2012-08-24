#ifndef HALIDE_VAR_H
#define HALIDE_VAR_H

#include "Util.h"
#include <assert.h>

namespace Halide {

    class Expr;
    class UniformImage;
    class DynImage;
    class RDom;

    // A variable
    class Var {
    public:
        Var() : _name(uniqueName('v')) { }
        Var(const std::string &name) : _name(name) { }

        const std::string &name() const {return _name;}

        bool operator==(const Var &other) const {
            return name() == other.name();
        }
        
    private:
        std::string _name;
    };

    // A reduction variable
    class RVar {
    public:
        // Make a reduction variable 
        RVar();
        RVar(const RDom &, const Expr &min, const Expr &size);
        RVar(const RDom &, const Expr &min, const Expr &size, const std::string &name);
       
        //void bound(const Expr &min, const Expr &size);   // Connelly: bound() is not defined in Var.cpp

        const Expr &min() const;
        const Expr &size() const;
        const std::string &name() const;
        bool operator==(const RVar &other) const;

        operator Var() {return Var(name());}
	const RDom &domain() const;

	bool isDefined() const {return (bool)contents;}
    private:

        struct Contents;
        shared_ptr<Contents> contents;
    };

    // A reduction domain
    class RDom {
    public:
	RDom();

	RVar x, y, z, w;

	RDom(const UniformImage &im);
	RDom(const DynImage &im);

	RDom(const Expr &min, const Expr &size);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2,
	     const Expr &min3, const Expr &size3);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2,
	     const Expr &min3, const Expr &size3,
	     const Expr &min4, const Expr &size4);
	RDom(const Expr &min, const Expr &size,
	     const std::string &name);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2,
	     const std::string &name);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2,
	     const Expr &min3, const Expr &size3,
	     const std::string &name);
	RDom(const Expr &min1, const Expr &size1,
	     const Expr &min2, const Expr &size2,
	     const Expr &min3, const Expr &size3,
	     const Expr &min4, const Expr &size4,
	     const std::string &name);

        bool operator==(const RDom &other) const;

	const RVar &operator[](int i) const;
	bool isDefined() const {return (bool)contents;}

	int dimensions() const;

	operator Var() {assert(dimensions() == 1); return Var(x);}

      private:
	struct Contents;
	shared_ptr<Contents> contents;
    };

}

#endif
