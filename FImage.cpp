#include "FImage.h"
#include "Compiler.h"


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
    //assert(val>>32 == 0, "We only trust 32 bit values for now: 0x%lx", val); // make sure we have a safe 32 bit value
    if ((val>>32) != 0 && (val>>31) != -1) {
        printf("We only trust 32 bit values for now: 0x%llx\n", val);
    }
    node = IRNode::make(val);
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

// Print out an expression
void Expr::debug() {
    node->printExp(); 
    printf("\n");
}

Range::Range() {
}

Range::Range(int a, int b) : Expr(IRNode::make(Var)) {
    min = a;
    max = b;
}

// Make an LVal reference to a particular pixel. It can be used as an
// assignment target or cast to a load operation. In the future it may
// also serve as a marker for a site in an expression that can be
// fused.
LVal::LVal(FImage *im_, Range a) : 
    Expr((*im_)(Expr(a))), 
    im(im_) {
    vars.resize(1);
    vars[0] = a;
}

LVal::LVal(FImage *im_, Range a, Range b) : 
    Expr((*im_)(Expr(a), Expr(b))), 
    im(im_) {
    vars.resize(2);
    vars[0] = a;
    vars[1] = b;
}

LVal::LVal(FImage *im_, Range a, Range b, Range c) : 
    Expr((*im_)(Expr(a), Expr(b), Expr(c))), 
    im(im_) {
    vars.resize(3);
    vars[0] = a;
    vars[1] = b;
    vars[2] = c;
}

LVal::LVal(FImage *im_, Range a, Range b, Range c, Range d) : 
    Expr((*im_)(Expr(a), Expr(b), Expr(c), Expr(d))), 
    im(im_) {
    vars.resize(4);
    vars[0] = a;
    vars[1] = b;
    vars[2] = c;
    vars[3] = d;
}

void LVal::operator=(Expr e) {
    node = e.node;

    // Add this to the list of definitions of im
    im->definitions.push_back(*this);
}

FImage::FImage(uint32_t a) {
    size.resize(1);
    stride.resize(1);
    size[0] = a;
    stride[0] = 1;
    // TODO: enforce alignment, lazy allocation, etc, etc
    data = new float[a];
}

FImage::FImage(uint32_t a, uint32_t b) {
    size.resize(2);
    stride.resize(2);
    size[0] = a;
    size[1] = b;
    stride[0] = 1;
    stride[1] = a;
    data = new float[a*b];
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
    data = new float[a*b*c];
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
    data = new float[a*b*c*d];
}



// Generate an LVal reference to a location in this image that can be
// used as an assignment target, and can also be cast to the RVal
// version (a load of a computed address)
LVal FImage::operator()(Range a) {
    return LVal(this, a);
}

LVal FImage::operator()(Range a, Range b) {
    return LVal(this, a, b);
}

LVal FImage::operator()(Range a, Range b, Range c) {
    return LVal(this, a, b, c);
}

LVal FImage::operator()(Range a, Range b, Range c, Range d) {
    return LVal(this, a, b, c, d);
}

// Print out a particular definition. We assume the LVal has already been assigned to.
void LVal::debug() {
    printf("[");
    for (size_t i = 0; i < vars.size(); i++) {
        vars[i].node->printExp();
        printf(":[%d-%d], ", vars[i].min, vars[i].max);
    }
    printf("\b\b\n");
}

#define PTR_TO_INT(p)   ((int64_t)(p))
// Generate a rval reference that will turn into a load of a computed address.
Expr FImage::operator()(Expr a) {    
    a = a * (4 * stride[0]);
    Expr addr(PTR_TO_INT(data));
    addr = addr + a;
    return Expr(IRNode::make(Load, addr.node));
}

Expr FImage::operator()(Expr a, Expr b) {    
    a = a * (4 * stride[0]);
    b = b * (4 * stride[1]);
    Expr addr(PTR_TO_INT(data));
    addr = (addr + b) + a;
    return Expr(IRNode::make(Load, addr.node));
}

Expr FImage::operator()(Expr a, Expr b, Expr c) {    
    a = a * (4 * stride[0]);
    b = b * (4 * stride[1]);
    c = c * (4 * stride[2]);
    Expr addr(PTR_TO_INT(data));
    addr = ((addr + c) + b) + a;
    return Expr(IRNode::make(Load, addr.node));
}

Expr FImage::operator()(Expr a, Expr b, Expr c, Expr d) {    
    a = a * (4 * stride[0]);
    b = b * (4 * stride[1]);
    c = c * (4 * stride[2]);
    d = d * (4 * stride[3]);
    Expr addr(PTR_TO_INT(data));
    addr = (((addr + d) + c) + b) + a;
    return Expr(IRNode::make(Load, addr.node));
}

// Print out all the definitions of this FImage
void FImage::debug() {
    for (size_t i = 0; i < definitions.size(); i++) {
        definitions[i].debug();
    }
}

FImage &FImage::evaluate() {
    // For now we assume the sole definition of this FImage is a very
    // simple gather
    Compiler c;
    AsmX64 a;    
    printf("Compiling...\n");
    c.compileGather(&a, this);
    printf("Running...\n");
    a.run();

    return *this;
}


