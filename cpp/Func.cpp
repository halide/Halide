#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Function.h"
#include "Argument.h"
#include "Lower.h"
#include "CodeGen_X86.h"
#include "Image.h"
#include <iostream>

namespace Halide {

using namespace Internal;

using std::max;
using std::min;

void Func::set_dim_type(Var var, For::ForType t) {
    bool found = false;
    vector<Schedule::Dim> &dims = func.ptr->schedule.dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (dims[i].var == var) {
            found = true;
            dims[i].for_type = t;
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
}

Func::Func(IntrusivePtr<Internal::Function> f) : func(f) {
}

Func::Func(const string &name) : func(new Internal::Function) {
    func.ptr->name = name;
}

Func::Func() : func(new Internal::Function) {
    func.ptr->name = unique_name('f');
}
        
const string &Func::name() const {
    return func.ptr->name;
}

const vector<Var> &Func::args() const {
    return func.ptr->args;
}

Expr Func::value() const {
    return func.ptr->value;
}

const Schedule &Func::schedule() const {
    return func.ptr->schedule;
}

FuncRefVar Func::operator()(Var x) {
    return FuncRefVar(func, vec(x));
}

FuncRefVar Func::operator()(Var x, Var y) {
    return FuncRefVar(func, vec(x, y));
}

FuncRefVar Func::operator()(Var x, Var y, Var z) {
    return FuncRefVar(func, vec(x, y, z));
}

FuncRefVar Func::operator()(Var x, Var y, Var z, Var w) {
    return FuncRefVar(func, vec(x, y, z, w));
}
 
FuncRefExpr Func::operator()(Expr x) {
    return FuncRefExpr(func, vec(x));
}

FuncRefExpr Func::operator()(Expr x, Expr y) {
    return FuncRefExpr(func, vec(x, y));
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z) {
    return FuncRefExpr(func, vec(x, y, z));
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z, Expr w) {
    return FuncRefExpr(func, vec(x, y, z, w));
}  

void Func::define(const vector<Var> &args, Expr value) {
    assert(!func.ptr->value.defined() && "Function is already defined");
    func.ptr->value = value;
    func.ptr->args = args;
        
    for (size_t i = 0; i < args.size(); i++) {
        Schedule::Dim d = {args[i], For::Serial};
        func.ptr->schedule.dims.push_back(d);
    }        
}

Func &Func::split(Var old, Var outer, Var inner, Expr factor) {
    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    vector<Schedule::Dim> &dims = func.ptr->schedule.dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (dims[i].var == old) {
            found = true;
            dims[i].var = inner;
            dims.push_back(dims[dims.size()-1]);
            for (size_t j = dims.size(); j > i+1; j--) {
                dims[j-1] = dims[j-2];
            }
            dims[i+1].var = outer;
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
        
    // Add the split to the splits list
    Schedule::Split split = {old, outer, inner, factor};
    func.ptr->schedule.splits.push_back(split);
    return *this;
}

Func &Func::parallel(Var var) {
    set_dim_type(var, For::Parallel);
    return *this;
}

Func &Func::vectorize(Var var) {
    set_dim_type(var, For::Vectorized);
    return *this;
}

Func &Func::unroll(Var var) {
    set_dim_type(var, For::Unrolled);
    return *this;
}

Func &Func::compute_at(Func f, Var var) {
    string loop_level = f.name() + "." + var.name();
    func.ptr->schedule.compute_level = loop_level;
    if (func.ptr->schedule.store_level.empty()) {
        func.ptr->schedule.store_level = loop_level;
    }
    return *this;
}
        
Func &Func::compute_root() {
    func.ptr->schedule.compute_level = "<root>";
    func.ptr->schedule.store_level = "<root>";
    return *this;
}

Func &Func::store_at(Func f, Var var) {
    func.ptr->schedule.store_level = f.name() + "." + var.name();
    return *this;
}

Func &Func::store_root() {
    func.ptr->schedule.store_level = "<root>";
    return *this;
}

Func &Func::compute_inline() {
    func.ptr->schedule.compute_level = "";
    func.ptr->schedule.store_level = "";
    return *this;
}


FuncRefVar::FuncRefVar(IntrusivePtr<Internal::Function> f, const vector<Var> &a) : func(f), args(a) {
    assert(f.defined() && "Can't construct reference to undefined Func");
}           
    
void FuncRefVar::operator=(Expr e) {            
    Func(func).define(args, e);
}
    
FuncRefVar::operator Expr() {
    assert(func.ptr->value.defined() && "Can't call function with undefined value");
    vector<Expr> expr_args(args.size());
    for (size_t i = 0; i < expr_args.size(); i++) {
        expr_args[i] = Expr(args[i]);
    }
    return new Call(func.ptr->value.type(), func.ptr->name, expr_args, Call::Halide, func, Buffer());
}
    
FuncRefExpr::FuncRefExpr(IntrusivePtr<Internal::Function> f, const vector<Expr> &a) : func(f), args(a) {
    assert(f.defined() && "Can't construct reference to undefined Func");
}
    
void FuncRefExpr::operator=(Expr) {
    assert(false && "Reductions not yet implemented");
}

FuncRefExpr::operator Expr() {                
    assert(func.ptr->value.defined() && "Can't call function with undefined value");
    return new Call(func.ptr->value.type(), func.ptr->name, args, Call::Halide, func, Buffer());
}

namespace Internal {
};

Buffer Func::realize(int x_size, int y_size, int z_size, int w_size) {
    assert(func.defined() && "Can't realize NULL function handle");
    assert(value().defined() && "Can't realize undefined function");
    Type t = value().type();
    Buffer buf(t, x_size, y_size, z_size, w_size);
    realize(buf);
    return buf;
}

/*
class InferArguments : public IRVisitor {
public:
    vector<Argument> arg_types;
    vector<void *> arg_values;
    
private:
    void visit(const Load *op) {
        if (op->buffer.defined()) {
            
        }
    }

    void visit(const Store *op) {
        if (op->buffer.defined()) {
            
        }
    }
};
*/

void Func::realize(Buffer dst) {
    assert(func.defined() && "Can't realize NULL function handle");
    assert(value().defined() && "Can't realize undefined function");
    
    Stmt stmt = lower(*this);
    
    // Infer arguments
    vector<Argument> arg_types;

    Argument me = {name(), true, Int(1)};
    arg_types.push_back(me);

    // TODO: Assume we're jitting for x86 for now
    CodeGen_X86 cg;
    cg.compile(stmt, name(), arg_types);

    void *fn_ptr = cg.compile_to_function_pointer(true);
    typedef void (*wrapped_fn_type)(const void **);
    wrapped_fn_type wrapped = (wrapped_fn_type)fn_ptr;   

    const void *arg_values[] = {dst.raw_buffer()};

    wrapped(arg_values);


    
}

void Func::test() {

    Func f, g;
    Var x, y;
    f(x, y) = x * y;
    g(x, y) = f(x-1, y) + 2*f(x+1, y);
    

    //f.compute_root();

    Image<int> result = g.realize(5, 5);

    for (size_t y = 0; y < 5; y++) {
        for (size_t x = 0; x < 5; x++) {
            int correct = (x-1)*y + 2*(x+1)*y;
            assert(result(x, y) == correct);
        }
    }

    std::cout << "Func test passed" << std::endl;

}

}
