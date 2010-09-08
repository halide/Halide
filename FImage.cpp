#include "FImage.h"
#include "Compiler.h"


Expr::Expr(IRNode *n) : node(n) {}

Expr::Expr(int val) {
    node = IRNode::make(val);
}

Expr::Expr(float val) {
    node = IRNode::make(val);
}

void Expr::operator+=(Expr other) {
    node = IRNode::make(Plus, node, other.node);
}

void Expr::operator*=(Expr other) {
    node = IRNode::make(Times, node, other.node);
}

void Expr::operator/=(Expr other) {
    node = IRNode::make(Divide, node, other.node);
}

void Expr::operator-=(Expr other) {
    node = IRNode::make(Minus, node, other.node);
}


Expr operator+(Expr a, Expr b) {
    return Expr(IRNode::make(Plus, a.node, b.node));
}

Expr operator-(Expr a, Expr b) {
    return Expr(IRNode::make(Minus, a.node, b.node));
}

Expr operator*(Expr a, Expr b) {
    return Expr(IRNode::make(Times, a.node, b.node));
}

Expr operator/(Expr a, Expr b) {
    return Expr(IRNode::make(Divide, a.node, b.node));
}

void Expr::debug() {
    node->printExp(); 
    printf("\n");
}

Var::Var(int a, int b) : Expr(IRNode::make(UnboundVar)) {
    min = a;
    max = b;
}

LVal::LVal(FImage *im_, Var x_, Var y_, Var c_) : 
    Expr((*im_)(Expr(x_), Expr(y_), Expr(c_))), 
    im(im_), x(x_), y(y_), c(c_) {
    // TODO: right node casting an lval to an expression produces a
    // load, which does not attempt to fuse kernels in any way
}


void LVal::operator=(Expr e) {
    // Make a new version of the rhs with the variables bound appropriately
    // Don't worry about t for now. Now's a good time to optimize too.
    IRNode *ix, *iy, *ic;
    printf("Creating assignment\n");        
    ix = x.node->bind(x.node, y.node, NULL, c.node)->optimize();
    printf("Done binding x\n");
    iy = y.node->bind(x.node, y.node, NULL, c.node)->optimize();
    printf("Done binding y\n");
    ic = c.node->bind(x.node, y.node, NULL, c.node)->optimize();
    printf("Done binding c\n");
    printf("Done creating LHS\n");
    node = e.node->bind(x.node, y.node, NULL, c.node);
    printf("Done creating RHS\n");
    node = node->optimize();    
    printf("Done optimizing RHS...\n");
    x.node = ix;
    y.node = iy;
    c.node = ic;
    im->definitions.push_back(*this);
}


FImage::FImage(int w, int h, int c) {
    width = w;
    height = h;
    channels = c;
    data = new float[width*height*channels];
    // TODO: enforce alignment, lazy allocation
}

LVal FImage::operator()(Var x, Var y, Var c) {
    return LVal(this, x, y, c);
}

void LVal::debug() {
    printf("[");
    x.node->printExp();
    printf(":[%d-%d], ", x.min, x.max);
    y.node->printExp();
    printf(":[%d-%d], ", y.min, y.max);
    c.node->printExp();
    printf(":[%d-%d]] = ", c.min, c.max);
    node->printExp();
    printf("\n");
}


Expr FImage::operator()(Expr x, Expr y, Expr c) {    
    y = y * (4*width*channels);
    x = x * (4*channels);
    c = c * 4;
    Expr addr((int)data);
    addr = ((addr + y) + x) + c;
    return Expr(IRNode::make(Load, addr.node));
}


void FImage::debug() {
    for (size_t i = 0; i < definitions.size(); i++) {
        definitions[i].debug();
    }
}

void FImage::evaluate() {
    // just evaluate the first definition for now
    Compiler c;
    AsmX64 a;    
    printf("Calling compile...\n");
    c.compileEval(&a, this);
    printf("Running...\n");
    a.run();
}


