#ifndef HALIDE_VAR_H
#define HALIDE_VAR_H

#include <string>
#include "Util.h"
#include "IR.h"
#include <sstream>

namespace Halide {

    class Var {
        std::string _name;
    public:
        Var(const std::string &n) : _name(n) {}
        Var() : _name(Internal::unique_name('v')) {}
        const std::string &name() const {return _name;}
        bool operator==(const Var &other) {return _name == other._name;}
        bool operator!=(const Var &other) {return _name != other._name;}

        static Var implicit(int n) {
            std::ostringstream str;
            str << "iv." << n;
            return Var(str.str());
        }

        operator Expr() {
            return new Internal::Variable(Int(32), name());
        }
    };


}

#endif
