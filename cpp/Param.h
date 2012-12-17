#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

namespace Halide {

class ScalarParam {
    
};

template<typename T>
class Param {
    
};

class ImageParam {
    string _name;
    Type _type;
    Buffer buffer;
public:
    ImageParam(Type t) : _name(unique_name('m')), _type(t) {}
    ImageParam(Type t, const string &n) : _name(n), _type(t) {}

    const string &name() {return _name;}
    int dimensions() {return dims;}
    Type type() {return _type;}

    void bind(Buffer b) {
        assert(_type == b.type());
        buffer = b
    }

    Expr operator()(Expr x) {
        vector<Expr> args;
        args.push_back(x);
        return new Call(_type, name(), args, Call::Image, NULL, Buffer(), *this);
    }

    Expr operator()(Expr x, Expr y) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        return new Call(_type, name(), args, Call::Image, NULL, Buffer(), *this);
    }

    Expr operator()(Expr x, Expr y, Expr z) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        return new Call(_type, name(), args, Call::Image, NULL, Buffer(), *this);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        return new Call(_type, name(), args, Call::Image, NULL, Buffer(), *this);
    }
   
};

}

#endif
