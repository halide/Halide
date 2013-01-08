#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

#include "IR.h"
#include "Var.h"
#include <sstream>
#include <vector>

namespace Halide {

template<typename T>
class Param {
    Internal::Parameter param;
public:
    Param() : param(type_of<T>(), false) {}
    Param(const std::string &n) : param(type_of<T>(), false, n) {}

    const std::string &name() const {
        return param.name();
    }

    T get() const {
        return param.get_scalar<T>();
    }

    void set(T val) {
        param.set_scalar<T>(val);
    }

    Type type() const {
        return type_of<T>();
    }

    bool defined() const {
        return param.defined();
    }

    operator Expr() const {
        return new Variable(type_of<T>(), name(), param);
    }

    operator Argument() const {
        return Argument(name(), false, type());
    }
};

class ImageParam {
    Internal::Parameter param;
    int dims;
public:
    ImageParam() {}
    ImageParam(Type t, int d) : param(t, true), dims(d) {}
    ImageParam(Type t, int d, const std::string &n) : param(t, true, n), dims(d) {}
    ImageParam(Internal::Parameter p, int d) : param(p), dims(d) {}

    const std::string &name() const {
        return param.name();
    }

    Type type() const {
        return param.type();
    }

    void set(Buffer b) {
        param.set_buffer(b);
    }
    
    Buffer get() const {
        return param.get_buffer();
    }

    bool defined() const {
        return param.defined();
    }

    Expr extent(int x) const {
        std::ostringstream s;
        s << name() << ".extent." << x;
        return new Variable(Int(32), s.str(), param);
    }

    int dimensions() const {
        return dims;
    };

    Expr width() const {
        return extent(0);
    }

    Expr height() const {
        return extent(1);
    }
    
    Expr channels() const {
        return extent(2);
    }

    Expr operator()() const {
        assert(dimensions() >= 0);
        std::vector<Expr> args;
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(param, args);
    }

    Expr operator()(Expr x) const {
        assert(dimensions() >= 1);
        std::vector<Expr> args;
        args.push_back(x);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y) const {
        assert(dimensions() >= 2);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        assert(dimensions() >= 3);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        assert(dimensions() >= 4);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(param, args);
    }

    operator Argument() const {
        return Argument(name(), true, type());
    }   

    operator Expr() const {
        return (*this)();
    }
};

}

#endif
