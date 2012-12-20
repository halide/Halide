#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

#include "IR.h"
#include <sstream>

namespace Halide {

template<typename T>
class Param {
    Internal::Parameter param;
public:
    Param() : param(type_of<T>()) {}
    Param(const string &n) : param(type_of<T>(), n) {}

    const string &name() const {
        return param.name();
    }

    T get() const {
        return param.get_scalar<T>();
    }

    void set(T val) {
        param.set_scalar<T>(val);
    }

    Type type() {
        return type_of<T>();
    }

    bool defined() const {
        return param.defined();
    }

    operator Expr() const {
        return new Variable(type_of<T>(), name(), param);
    }
};

class ImageParam {
    Internal::Parameter param;
public:
    ImageParam() {}
    ImageParam(Type t) : param(t) {}
    ImageParam(Type t, const string &n) : param(t, n) {}
    ImageParam(Internal::Parameter p) : param(p) {}

    const string &name() const {
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

    Expr extent(int x) {
        std::ostringstream s;
        s << name() << ".extent." << x;
        return new Variable(Int(32), s.str(), param);
    }

    Expr width() {
        return extent(0);
    }

    Expr height() {
        return extent(1);
    }
    
    Expr channels() {
        return extent(2);
    }

    Expr operator()(Expr x) {
        vector<Expr> args;
        args.push_back(x);
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        return new Call(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        return new Call(param, args);
    }
   
};

}

#endif
