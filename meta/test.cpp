// This is a test to see if metaprogramming is viable for us. The goal
// is to elevate our ASTs into the type system in the usual
// metaprogramming way, and make the c++ compiler spit out a linker
// error about the missing function that evaluates our
// image. Fortunately, this missing function's body can be deduced
// from its type by an external program, and then linking can be rerun
// against our generated object code.
//
// The conclusion was that metaprogramming is not a good fit because
// in the expression: f(x, y) = x*2 + y*2 + g(x), x, y, f, and g must
// all have be unique singleton types in order to distinguish between
// them in the type system. This means declaring things like this:
// Var<0> x; Var<1> y; instead of the better Var x, y. If every FImage
// is its own type, and must be known as that type at compile time in
// order to properly add definitions to it, then functions that take
// FImages must be templated. It's also a pain to lift constants into
// the type system.

// throw an error at link time letting us know which expressions are in use
template<typename A, typename B>
extern void new_definition(A a, B b);

template<typename A>
struct FExpr {
    typedef A Contents;
    A contents;
    template<typename B>
    void operator=(FExpr<B> b) {
        new_definition(contents, b.contents);
    }
};

template<int id, typename A, typename B, typename C>
struct Sample {
    A a; B b; C c;
};

struct X {};
struct Y {};
struct C {};
FExpr<X> x;
FExpr<Y> y;
FExpr<C> c;

template<int x>
struct _Int {
};

#define Int(x) (FExpr<_Int<x> >())

template<int id>
struct FImage {
    template<typename A, typename B, typename C>
    FExpr<Sample<id, A, B, C> > operator()(FExpr<A> a, FExpr<B> b, FExpr<C> c) {
        return FExpr<Sample<id, A, B, C> >();
    }
    

};

template<typename A, typename B>
struct Plus {A a; B b;};

template<typename A, typename B>
struct Times {A a; B b;};

template<typename A, typename B>
FExpr<Plus<A, B> > operator+(FExpr<A> a, FExpr<B> b) {
    return FExpr<Plus<A, B> >();
}

template<typename A, typename B>
FExpr<Times<A, B> > operator*(FExpr<A> a, FExpr<B> b) {
    return FExpr<Times<A, B> >();
}







int main(int argc, char ** argv) {
    FImage<0> out;
    FImage<1> in1;
    FImage<2> in2;

    out(x, y, c) = in1(x, y, c) + in2(y, x, c)*Int(7);


    return 0;
}
