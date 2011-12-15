#ifndef FIMAGE_VAR_H
#define FIMAGE_VAR_H

#include "Util.h"

namespace FImage {

    class Expr;

    // A variable
    class Var {
    public:
        Var() : _name(uniqueName('v')) {}
        Var(const std::string &name) : _name(name) {}

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
        RVar(const Expr &min, const Expr &size);
        RVar(const Expr &min, const Expr &size, const std::string &name);
       
        const Expr &min() const;
        const Expr &size() const;
        const std::string &name() const;
        bool operator==(const RVar &other) const;
        
    private:

        struct Contents;
        std::shared_ptr<Contents> contents;
    };

}

#endif
