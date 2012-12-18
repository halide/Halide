#include "IR.h"
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
#include <fstream>

namespace Halide {

using namespace Internal;

using std::max;
using std::min;

void Func::set_dim_type(Var var, For::ForType t) {
    bool found = false;
    vector<Schedule::Dim> &dims = func.schedule().dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (dims[i].var == var.name()) {
            found = true;
            dims[i].for_type = t;
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
}

Func::Func(Internal::Function f) : func(f), function_ptr(NULL) {
}

Func::Func(const string &name) : func(name), function_ptr(NULL) {
}

Func::Func() : func(unique_name('f')), function_ptr(NULL) {
}
        
const string &Func::name() const {
    return func.name();
}

Expr Func::value() const {
    return func.value();
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


Func &Func::split(Var old, Var outer, Var inner, Expr factor) {
    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    vector<Schedule::Dim> &dims = func.schedule().dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (dims[i].var == old.name()) {
            found = true;
            dims[i].var = inner.name();
            dims.push_back(dims[dims.size()-1]);
            for (size_t j = dims.size(); j > i+1; j--) {
                dims[j-1] = dims[j-2];
            }
            dims[i+1].var = outer.name();
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
        
    // Add the split to the splits list
    Schedule::Split split = {old.name(), outer.name(), inner.name(), factor};
    func.schedule().splits.push_back(split);
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

Func &Func::vectorize(Var var, int factor) {
    Var tmp;
    split(var, var, tmp, factor);
    vectorize(tmp);
    return *this;
}

Func &Func::unroll(Var var, int factor) {
    Var tmp;
    split(var, var, tmp, factor);
    unroll(tmp);
    return *this;
}

Func &Func::compute_at(Func f, Var var) {
    string loop_level = f.name() + "." + var.name();
    func.schedule().compute_level = loop_level;
    if (func.schedule().store_level.empty()) {
        func.schedule().store_level = loop_level;
    }
    return *this;
}
        
Func &Func::compute_root() {
    func.schedule().compute_level = "<root>";
    func.schedule().store_level = "<root>";
    return *this;
}

Func &Func::store_at(Func f, Var var) {
    func.schedule().store_level = f.name() + "." + var.name();
    return *this;
}

Func &Func::store_root() {
    func.schedule().store_level = "<root>";
    return *this;
}

Func &Func::compute_inline() {
    func.schedule().compute_level = "";
    func.schedule().store_level = "";
    return *this;
}

Func &Func::bound(Var var, Expr min, Expr extent) {
    Schedule::Bound b = {var.name(), min, extent};
    func.schedule().bounds.push_back(b);
    return *this;
}

Func &Func::tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor) {
    std::cout << "Tile: " << x.name() << ", " << y.name() << ", " << xo.name() << ", " << yo.name() << ", " << xi.name() << ", " << yi.name() << std::endl;
    split(x, xo, xi, xfactor);
    split(y, yo, yi, yfactor);
    reorder(xi, yi, xo, yo);
    return *this;
}

Func &Func::reorder(Var x, Var y) {
    vector<Schedule::Dim> &dims = func.schedule().dims;
    bool found_y = false;
    size_t y_loc = 0;
    std::cout << "Swapping " << x.name() << " and " << y.name() << std::endl;
    for (size_t i = 0; i < dims.size(); i++) {
        std::cout << "Considering " << dims[i].var << std::endl;
        if (dims[i].var == y.name()) {
            found_y = true;
            y_loc = i;
        } else if (dims[i].var == x.name()) {
            if (found_y) std::swap(dims[i], dims[y_loc]);
            return *this;
        }
    }
    assert(false && "Could not find these variables to reorder in schedule");
    return *this;
}
    

Func &Func::reorder(Var x, Var y, Var z) {
    return reorder(x, y).reorder(x, z).reorder(y, z);
}

Func &Func::reorder(Var x, Var y, Var z, Var w) {
    return reorder(x, y).reorder(x, z).reorder(x, w).reorder(y, z, w);
}

Func &Func::reorder(Var x, Var y, Var z, Var w, Var t) {
    return reorder(x, y).reorder(x, z).reorder(x, w).reorder(x, t).reorder(y, z, w, t);
}


FuncRefVar::FuncRefVar(Internal::Function f, const vector<Var> &a) : func(f) {
    assert(f.defined() && "Can't construct reference to undefined Func");
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = a[i].name();
    }
}           
    
void FuncRefVar::operator=(Expr e) {            
    func.define(args, e);
}
    
FuncRefVar::operator Expr() {
    assert(func.value().defined() && "Can't call function with undefined value");
    vector<Expr> expr_args(args.size());
    for (size_t i = 0; i < expr_args.size(); i++) {
        expr_args[i] = new Variable(Int(32), args[i]);
    }
    return new Call(func.value().type(), func.name(), expr_args, Call::Halide, func, Buffer());
}
    
FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<Expr> &a) : func(f), args(a) {
    assert(f.defined() && "Can't construct reference to undefined Func");
}
    
void FuncRefExpr::operator=(Expr) {
    assert(false && "Reductions not yet implemented");
}

FuncRefExpr::operator Expr() {                
    assert(func.value().defined() && "Can't call function with undefined value");
    return new Call(func.value().type(), func.name(), args, Call::Halide, func, Buffer());
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


class InferArguments : public IRVisitor {
public:
    vector<Argument> arg_types;
    vector<const void *> arg_values;
    
private:
    void visit(const Load *op) {
        if (op->image.defined()) {            
            Argument arg = {op->image.name(), true, Int(1)};
            bool already_included = false;
            for (size_t i = 0; i < arg_types.size(); i++) {
                if (arg_types[i].name == op->image.name()) {
                    already_included = true;
                }
            }
            if (!already_included) {
                //std::cout << "Adding buffer " << op->image.name() << " to the arguments" << std::endl;
                arg_types.push_back(arg);
                arg_values.push_back(op->image.raw_buffer());
            } else {
                //std::cout << "Not adding buffer " << op->image.name() << " to the arguments" << std::endl;
            }
        }
    }
};

Stmt Func::lower() {
    return Halide::Internal::lower(func);
}

void Func::realize(Buffer dst) {
    if (!function_ptr) {
   
        assert(func.defined() && "Can't realize NULL function handle");
        assert(value().defined() && "Can't realize undefined function");
        
        Stmt stmt = lower();
        
        // Infer arguments
        InferArguments infer_args;
        stmt.accept(&infer_args);
        
        Argument me = {name(), true, Int(1)};
        infer_args.arg_types.push_back(me);
        arg_values = infer_args.arg_values;
        arg_values.push_back(dst.raw_buffer());
        
        // TODO: Assume we're jitting for x86 for now
        CodeGen_X86 cg;
        cg.compile(stmt, name(), infer_args.arg_types);
        
        /*
          cg.compile_to_native(name() + ".s", true);
          cg.compile_to_bitcode(name() + ".bc");
          std::ofstream stmt_debug((name() + ".stmt").c_str());
          stmt_debug << stmt;
        */

        void *fn_ptr = cg.compile_to_function_pointer(true);
        typedef void (*wrapped_fn_type)(const void **);
        function_ptr = (wrapped_fn_type)fn_ptr;   
    } else {
        // Update the address of the buffer we're realizing into
        arg_values[arg_values.size()-1] = dst.raw_buffer();
    }

    function_ptr(&(arg_values[0]));
    
}

void Func::test() {

    Image<int> input(7, 5);
    for (size_t y = 0; y < 5; y++) {
        for (size_t x = 0; x < 5; x++) {
            input(x, y) = x*y + 10/(y+3);
        }
    }


    Func f, g;
    Var x, y;
    f(x, y) = input(x+1, y) + input(x+1, y)*3 + 1;
    g(x, y) = f(x-1, y) + 2*f(x+1, y);
    

    f.compute_root();

    Image<int> result = g.realize(5, 5);

    for (size_t y = 0; y < 5; y++) {
        for (size_t x = 0; x < 5; x++) {
            int correct = (4*input(x, y)+1) + 2*(4*input(x+2, y)+1);
            assert(result(x, y) == correct);
        }
    }

    std::cout << "Func test passed" << std::endl;

}

}
