#include "FImage.h"
#include "AsmX64Compiler.h"


#ifndef _MSC_VER
#include <sys/time.h>
// This returns unsigned long, not as-yet-undefined DWORD, here for expedience
unsigned long timeGetTime()
{
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    // TODO: this almost surely wraps around in many situations
    return tv.tv_sec*1000 + tv.tv_usec/1000;
}
#endif //!_MSC_VER


// An Expr is a wrapper around the IRNode structure used by the compiler
Expr::Expr() {}

Expr::Expr(IRNode::Ptr n) : node(n) {}

Expr::Expr(int64_t val) {
    node = IRNode::make(val);
}

Expr::Expr(void *val) {
    // assume a 64-bit OS
    node = IRNode::make((int64_t)val);
}

Expr::Expr(int32_t val) {
    node = IRNode::make((int64_t)val);
}

Expr::Expr(uint32_t val) {
    node = IRNode::make((int64_t)val);
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

Expr operator>(Expr a, Expr b) {
    return Expr(IRNode::make(GT, a.node, b.node));
}

Expr operator<(Expr a, Expr b) {
    return Expr(IRNode::make(LT, a.node, b.node));
}

Expr operator>=(Expr a, Expr b) {
    return Expr(IRNode::make(GTE, a.node, b.node));
}

Expr operator<=(Expr a, Expr b) {
    return Expr(IRNode::make(LTE, a.node, b.node));
}

Expr operator!=(Expr a, Expr b) {
    return Expr(IRNode::make(NEQ, a.node, b.node));
}

Expr operator==(Expr a, Expr b) {
    return Expr(IRNode::make(EQ, a.node, b.node));
}

Expr select(Expr cond, Expr thenCase, Expr elseCase) {
    IRNode::Ptr t = IRNode::make(And, cond.node, thenCase.node);
    IRNode::Ptr e = IRNode::make(Nand, cond.node, elseCase.node);
    IRNode::Ptr result = IRNode::make(Or, t, e);
    return Expr(result);
}

// Print out an expression
void Expr::debug() {
    node->printExp(); 
    printf("\n");
}

Var::Var(int a, int b) : Expr(IRNode::make(Variable)) {
    node->interval.setBounds(a, b-1);
    var = node->data<Variable>();
    var->unroll = 1;
    var->vectorize = 1;
    var->parallelize = 1;
    var->fuseLoops = false;
    var->order = Parallel;
    var->loopNesting = 0;
}

// Make an MemRef reference to a particular pixel. It can be used as an
// assignment target or cast to a load operation. In the future it may
// also serve as a marker for a site in an expression that can be
// fused.
MemRef::MemRef(FImage *im_, Expr a) :
    im(im_) {

    // If you upcast this to an Expr it gets treated as a load
    a = a * (4*im->stride[0]);
    Expr addr = Expr(im->data) + a;
    node = IRNode::make(Load, addr.node);

    indices.resize(1);
    indices[0] = a;
}

MemRef::MemRef(FImage *im_, Expr a, Expr b) : 
    im(im_) {

    // If you upcast this to an Expr it gets treated as a load
    a = a * (4*im->stride[0]);
    b = b * (4*im->stride[1]);
    Expr addr = (Expr(im->data) + a) + b;
    node = IRNode::make(Load, addr.node);

    indices.resize(2);
    indices[0] = a;
    indices[1] = b;
}

MemRef::MemRef(FImage *im_, Expr a, Expr b, Expr c) : 
    im(im_) {

    // If you upcast this to an Expr it gets treated as a load
    a = a * (4*im->stride[0]);
    b = b * (4*im->stride[1]);
    c = c * (4*im->stride[2]);
    Expr addr = ((Expr(im->data) + a) + b) + c;
    node = IRNode::make(Load, addr.node);

    indices.resize(3);
    indices[0] = a;
    indices[1] = b;
    indices[2] = c;
}

MemRef::MemRef(FImage *im_, Expr a, Expr b, Expr c, Expr d) : 
    im(im_) {

    // If you upcast this to an Expr it gets treated as a load
    a = a * (4*im->stride[0]);
    b = b * (4*im->stride[1]);
    c = c * (4*im->stride[2]);
    d = d * (4*im->stride[3]);
    Expr addr = (((Expr(im->data) + a) + b) + c) + d;
    node = IRNode::make(Load, addr.node);

    indices.resize(4);
    indices[0] = a;
    indices[1] = b;
    indices[2] = c;
    indices[3] = d;
}

void MemRef::operator=(const Expr &e) {
    // We were a load - convert to a store instead
    node = IRNode::make(Store, node->inputs[0], e.node, node->ival);

    // Add this to the list of definitions of im
    printf("Adding a definition\n");
    im->definitions.push_back(*this);
}

FImage::FImage(uint32_t a) {
    size.resize(1);
    stride.resize(1);
    size[0] = a;
    stride[0] = 1;
    // TODO: enforce alignment, lazy allocation, etc, etc
    buffer.reset(new vector<float>(a + 8));
    data = &(*buffer)[0] + 4;
}

FImage::FImage(uint32_t a, uint32_t b) {
    size.resize(2);
    stride.resize(2);
    size[0] = a;
    size[1] = b;
    stride[0] = 1;
    stride[1] = a;
    buffer.reset(new vector<float>(a*b + 8));
    data = &(*buffer)[0] + 4;
}

FImage::FImage(uint32_t a, uint32_t b, uint32_t c) {
    size.resize(3);
    stride.resize(3);
    size[0] = a;
    size[1] = b;
    size[2] = c;
    stride[0] = 1;
    stride[1] = a;
    stride[2] = a*b;
    buffer.reset(new vector<float>(a*b*c + 8));
    data = &(*buffer)[0] + 4;

}

FImage::FImage(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    size.resize(4);
    stride.resize(4);
    size[0] = a;
    size[1] = b;
    size[2] = c;
    size[3] = d;
    stride[0] = 1;
    stride[1] = a;
    stride[2] = a*b;
    stride[3] = a*b*c;
    buffer.reset(new vector<float>(a*b*c*d + 8));
    data = &(*buffer)[0] + 4;
}

FImage::~FImage() {
    //delete[] (data-4);
}

// Generate an MemRef reference to a (computed) location in this image
// that can be used as an assignment target, and can also be cast to
// the Expr version (a load of a computed address)
MemRef FImage::operator()(Expr a) {
    return MemRef(this, a);
}

MemRef FImage::operator()(Expr a, Expr b) {
    return MemRef(this, a, b);
}

MemRef FImage::operator()(Expr a, Expr b, Expr c) {
    return MemRef(this, a, b, c);
}

MemRef FImage::operator()(Expr a, Expr b, Expr c, Expr d) {
    return MemRef(this, a, b, c, d);
}

// Print out a particular definition. We assume the MemRef has already been assigned to.
void MemRef::debug() {
    printf("[");
    for (size_t i = 0; i < indices.size(); i++) {
        indices[i].node->printExp();
        printf(", ");
    }
    printf("\b\b]\n");
}


// Print out all the definitions of this FImage
void FImage::debug() {
    for (size_t i = 0; i < definitions.size(); i++) {
        definitions[i].debug();
    }
}

FImage &FImage::evaluate(time_t *time) {

    AsmX64Compiler *c = new AsmX64Compiler();
    printf("Compiling...\n"); fflush(stdout);
    c->compile(this);
    printf("Running...\n"); fflush(stdout);
    time_t t0 = timeGetTime();
    c->run();
    time_t t1 = timeGetTime();
    if (time) time[0] = t1-t0;
    printf("Done\n"); fflush(stdout);

    return *this;
}


