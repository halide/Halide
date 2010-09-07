#ifndef FIMAGE_H
#define FIMAGE_H

#include <vector>
using namespace std;

class IRNode;
class FImage;

class Expr {
public:
    Expr(IRNode *n);
    Expr(int);
    Expr(float);
    Expr operator+(Expr);
    Expr operator-(Expr);
    Expr operator*(Expr);
    Expr operator/(Expr);
    IRNode *node;
    void debug();
};

class Var : public Expr {
public:
    Var(int a, int b);
    int min, max;
};


class LVal : public Expr {
public:
    LVal(FImage *, Var, Var, Var);
    void operator=(Expr);
    void debug();

    FImage *im;
    Var x, y, c;
};

class FImage {
public:
    FImage(int, int, int);

    LVal operator()(Var, Var, Var);
    Expr operator()(Expr, Expr, Expr);

    float operator()(int, int, int);

    void evaluate();

    int width, height, channels;

    // public for now, to allow for messing with and debugging
    float *data;

    vector<LVal> definitions;
    
    void debug();
};

#endif
