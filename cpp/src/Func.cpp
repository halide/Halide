#include "IR.h"
#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Function.h"
#include "Argument.h"
#include "Lower.h"
#include "StmtCompiler.h"
#include "CodeGen_C.h"
#include "Image.h"
#include "Param.h"
#include "Log.h"
#include <iostream>
#include <fstream>

namespace Halide {

using std::max;
using std::min;
using std::make_pair;
using std::string;
using std::vector;
using std::pair;
using std::ofstream;

using namespace Internal;

Func::Func(const string &name) : func(name), error_handler(NULL), custom_malloc(NULL), custom_free(NULL) {
}

Func::Func() : func(unique_name('f')), error_handler(NULL), custom_malloc(NULL), custom_free(NULL) {
}

Func::Func(Expr e) : func(unique_name('f')), error_handler(NULL), custom_malloc(NULL), custom_free(NULL) {
    (*this)() = e;
}
        
const string &Func::name() const {
    return func.name();
}

Expr Func::value() const {
    return func.value();
} 

int Func::dimensions() const {
    if (!func.value().defined()) return 0;
    return (int)func.args().size();
}

FuncRefVar Func::operator()() {
    // Bulk up the argument list using implicit vars
    vector<Var> args;
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}

FuncRefVar Func::operator()(Var x) {
    // Bulk up the argument list using implicit vars
    vector<Var> args = vec(x);
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}

FuncRefVar Func::operator()(Var x, Var y) {
    vector<Var> args = vec(x, y);
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}

FuncRefVar Func::operator()(Var x, Var y, Var z) {
    vector<Var> args = vec(x, y, z);
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}

FuncRefVar Func::operator()(Var x, Var y, Var z, Var w) {
    vector<Var> args = vec(x, y, z, w);
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}

FuncRefVar Func::operator()(vector<Var> args) {
    add_implicit_vars(args);
    return FuncRefVar(func, args);
}
 
FuncRefExpr Func::operator()(Expr x) {
    vector<Expr> args = vec(x);
    add_implicit_vars(args);
    return FuncRefExpr(func, args);
}

FuncRefExpr Func::operator()(Expr x, Expr y) {
    vector<Expr> args = vec(x, y);
    add_implicit_vars(args);
    return FuncRefExpr(func, args);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z) {
    vector<Expr> args = vec(x, y, z);
    add_implicit_vars(args);
    return FuncRefExpr(func, args);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z, Expr w) {
    vector<Expr> args = vec(x, y, z, w);
    add_implicit_vars(args);
    return FuncRefExpr(func, args);
}  

FuncRefExpr Func::operator()(vector<Expr> args) {
    add_implicit_vars(args);
    return FuncRefExpr(func, args);
}

void Func::add_implicit_vars(vector<Var> &args) {
    int i = 0;    
    while ((int)args.size() < dimensions()) {        
        Internal::log(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
        args.push_back(Var::implicit(i++));
    }
}
    
void Func::add_implicit_vars(vector<Expr> &args) {
    int i = 0;
    while ((int)args.size() < dimensions()) {
        Internal::log(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
        args.push_back(Var::implicit(i++));
    }
}


namespace {
bool var_name_match(string candidate, string var) {
    if (candidate == var) return true;
    return Internal::ends_with(candidate, "." + var);
}
}

void ScheduleHandle::set_dim_type(Var var, For::ForType t) {
    bool found = false;
    vector<Schedule::Dim> &dims = schedule.dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].for_type = t;
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
}

ScheduleHandle &ScheduleHandle::split(Var old, Var outer, Var inner, Expr factor) {
    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string inner_name, outer_name, old_name;
    vector<Schedule::Dim> &dims = schedule.dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old.name())) {
            found = true;
            old_name = dims[i].var;
            inner_name = old_name + "." + inner.name();
            outer_name = old_name + "." + outer.name();
            dims[i].var = inner_name;
            dims.push_back(dims[dims.size()-1]);
            for (size_t j = dims.size(); j > i+1; j--) {
                dims[j-1] = dims[j-2];
            }
            dims[i+1].var = outer_name;
        }
    }
        
    assert(found && "Could not find dimension in argument list for function");
        
    // Add the split to the splits list
    Schedule::Split split = {old_name, outer_name, inner_name, factor};
    schedule.splits.push_back(split);
    return *this;
}

ScheduleHandle &ScheduleHandle::parallel(Var var) {
    set_dim_type(var, For::Parallel);
    return *this;
}

ScheduleHandle &ScheduleHandle::vectorize(Var var) {
    set_dim_type(var, For::Vectorized);
    return *this;
}

ScheduleHandle &ScheduleHandle::unroll(Var var) {
    set_dim_type(var, For::Unrolled);
    return *this;
}

ScheduleHandle &ScheduleHandle::vectorize(Var var, int factor) {
    Var tmp;
    split(var, var, tmp, factor);
    vectorize(tmp);
    return *this;
}

ScheduleHandle &ScheduleHandle::unroll(Var var, int factor) {
    Var tmp;
    split(var, var, tmp, factor);
    unroll(tmp);
    return *this;
}

ScheduleHandle &ScheduleHandle::bound(Var var, Expr min, Expr extent) {
    Schedule::Bound b = {var.name(), min, extent};
    schedule.bounds.push_back(b);
    return *this;
}

ScheduleHandle &ScheduleHandle::tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor) {
    split(x, xo, xi, xfactor);
    split(y, yo, yi, yfactor);
    reorder(xi, yi, xo, yo);
    return *this;
}

ScheduleHandle &ScheduleHandle::tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor) {
    split(x, x, xi, xfactor);
    split(y, y, yi, yfactor);
    reorder(xi, yi, x, y);
    return *this;
}

ScheduleHandle &ScheduleHandle::reorder(Var x, Var y) {
    vector<Schedule::Dim> &dims = schedule.dims;
    bool found_y = false;
    size_t y_loc = 0;
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, y.name())) {
            found_y = true;
            y_loc = i;
        } else if (var_name_match(dims[i].var, x.name())) {
            if (found_y) std::swap(dims[i], dims[y_loc]);
            return *this;
        }
    }
    assert(false && "Could not find these variables to reorder in schedule");
    return *this;
}
    

ScheduleHandle &ScheduleHandle::reorder(Var x, Var y, Var z) {
    return reorder(x, y).reorder(x, z).reorder(y, z);
}

ScheduleHandle &ScheduleHandle::reorder(Var x, Var y, Var z, Var w) {
    return reorder(x, y).reorder(x, z).reorder(x, w).reorder(y, z, w);
}

ScheduleHandle &ScheduleHandle::reorder(Var x, Var y, Var z, Var w, Var t) {
    return reorder(x, y).reorder(x, z).reorder(x, w).reorder(x, t).reorder(y, z, w, t);
}


Func &Func::split(Var old, Var outer, Var inner, Expr factor) {
    ScheduleHandle(func.schedule()).split(old, outer, inner, factor);
    return *this;
}

Func &Func::parallel(Var var) {
    ScheduleHandle(func.schedule()).parallel(var);
    return *this;
}

Func &Func::vectorize(Var var) {
    ScheduleHandle(func.schedule()).vectorize(var);
    return *this;
}

Func &Func::unroll(Var var) {
    ScheduleHandle(func.schedule()).unroll(var);
    return *this;
}

Func &Func::vectorize(Var var, int factor) {
    ScheduleHandle(func.schedule()).vectorize(var, factor);
    return *this;
}

Func &Func::unroll(Var var, int factor) {
    ScheduleHandle(func.schedule()).unroll(var, factor);
    return *this;
}

Func &Func::bound(Var var, Expr min, Expr extent) {
    ScheduleHandle(func.schedule()).bound(var, min, extent);
    return *this;
}

Func &Func::tile(Var x, Var y, Var xo, Var yo, Var xi, Var yi, Expr xfactor, Expr yfactor) {
    ScheduleHandle(func.schedule()).tile(x, y, xo, yo, xi, yi, xfactor, yfactor);
    return *this;
}

Func &Func::tile(Var x, Var y, Var xi, Var yi, Expr xfactor, Expr yfactor) {
    ScheduleHandle(func.schedule()).tile(x, y, xi, yi, xfactor, yfactor);
    return *this;
}

Func &Func::reorder(Var x, Var y) {
    ScheduleHandle(func.schedule()).reorder(x, y);
    return *this;
}    

Func &Func::reorder(Var x, Var y, Var z) {
    ScheduleHandle(func.schedule()).reorder(x, y, z);
    return *this;
}

Func &Func::reorder(Var x, Var y, Var z, Var w) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w);
    return *this;
}

Func &Func::reorder(Var x, Var y, Var z, Var w, Var t) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t);
    return *this;
}

Func &Func::compute_at(Func f, RVar var) {
    return compute_at(f, Var(var.name()));
}

Func &Func::compute_at(Func f, Var var) {
    Schedule::LoopLevel loop_level(f.name(), var.name());
    func.schedule().compute_level = loop_level;
    if (func.schedule().store_level.is_inline()) {
        func.schedule().store_level = loop_level;
    }
    return *this;
}
        
Func &Func::compute_root() {
    func.schedule().compute_level = Schedule::LoopLevel::root();
    func.schedule().store_level = Schedule::LoopLevel::root();
    return *this;
}

Func &Func::store_at(Func f, RVar var) {
    return store_at(f, Var(var.name()));
}

Func &Func::store_at(Func f, Var var) {
    func.schedule().store_level = Schedule::LoopLevel(f.name(), var.name());
    return *this;
}

Func &Func::store_root() {
    func.schedule().store_level = Schedule::LoopLevel::root();
    return *this;
}

Func &Func::compute_inline() {
    func.schedule().compute_level = Schedule::LoopLevel();
    func.schedule().store_level = Schedule::LoopLevel();
    return *this;
}

void Func::debug_to_file(const string &filename) {
    func.debug_file() = filename;    
}

ScheduleHandle Func::update() {
    return ScheduleHandle(func.reduction_schedule());
}

FuncRefVar::FuncRefVar(Internal::Function f, const vector<Var> &a) : func(f) {
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = a[i].name();
    }
}           
    
namespace {
class CountImplicitVars : public Internal::IRVisitor {
public:
    int count;
    CountImplicitVars(Expr e) : count(0) {
        e.accept(this);
    }

    using IRVisitor::visit;

    void visit(const Variable *v) {
        if (v->name.size() > 3 && v->name.substr(0, 3) == "iv.") {
            int n = atoi(v->name.c_str()+3);
            if (n >= count) count = n+1;
        }
    }    
};
}

void FuncRefVar::add_implicit_vars(vector<string> &a, Expr e) {
    CountImplicitVars count(e);
    Internal::log(2) << "Adding " << count.count << " implicit vars to LHS of " << func.name() << "\n";
    for (int i = 0; i < count.count; i++) {
        a.push_back(Var::implicit(i).name());
    }    
}

void FuncRefVar::operator=(Expr e) {            
    // If the function has already been defined, this must actually be a reduction
    if (func.value().defined()) {
        FuncRefExpr(func, args) = e;
        return;
    }

    // Find implicit args in the expr and add them to the args list before calling define
    vector<string> a = args;
    add_implicit_vars(a, e);
    func.define(a, e);
}
    
void FuncRefVar::operator+=(Expr e) {
    // This is actually a reduction
    FuncRefExpr(func, args) += e;
}

void FuncRefVar::operator*=(Expr e) {
    // This is actually a reduction
    FuncRefExpr(func, args) *= e;
}

void FuncRefVar::operator-=(Expr e) {
    // This is actually a reduction
    FuncRefExpr(func, args) -= e;
}

void FuncRefVar::operator/=(Expr e) {
    // This is actually a reduction
    FuncRefExpr(func, args) /= e;
}

FuncRefVar::operator Expr() const {
    assert(func.value().defined() && "Can't call function with undefined value");
    vector<Expr> expr_args(args.size());
    for (size_t i = 0; i < expr_args.size(); i++) {
        expr_args[i] = Var(args[i]);
    }
    return new Call(func, expr_args);
}

FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<Expr> &a) : func(f), args(a) {
    for (size_t i = 0; i < args.size(); i++) {
        args[i] = cast<int>(args[i]);
    }
}

FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<string> &a) : func(f) {
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = Var(a[i]);
    }
}
    
void FuncRefExpr::add_implicit_vars(vector<Expr> &a, Expr e) {
    CountImplicitVars f(e);
    // Implicit vars are also allowed in the lhs of a reduction. E.g.:
    // f(x, y) = x+y
    // g(x, y) = 0
    // g(f(r.x)) = 1   (this means g(f(r.x, i0), i0) = 1)

    for (size_t i = 0; i < args.size(); i++) {
        args[i].accept(&f);
    }
    Internal::log(2) << "Adding " << f.count << " implicit vars to LHS of " << func.name() << "\n";
    for (int i = 0; i < f.count; i++) {
        a.push_back(Var::implicit(i));
    }
}

void FuncRefExpr::operator=(Expr e) {
    assert(func.value().defined() && 
           "Can't add a reduction definition to an undefined function");
    
    vector<Expr> a = args;
    add_implicit_vars(a, e);

    func.define_reduction(args, e);
}

// Inject a suitable base-case definition given a reduction
// definition. This is a helper for FuncRefExpr::operator+= and co.
void define_base_case(Internal::Function func, const vector<Expr> &a, Expr e) {
    if (func.value().defined()) return;
    vector<Var> pure_args(a.size());

    // Reuse names of existing pure args
    for (size_t i = 0; i < a.size(); i++) {
        if (const Variable *v = a[i].as<Variable>()) {
            if (!v->param.defined()) pure_args[i] = Var(v->name);
        }
    }    

    FuncRefVar(func, pure_args) = e;
}

void FuncRefExpr::operator+=(Expr e) {
    vector<Expr> a = args;
    add_implicit_vars(a, e);
    define_base_case(func, a, cast(e.type(), 0));
    (*this) = Expr(*this) + e;
}

void FuncRefExpr::operator*=(Expr e) {
    vector<Expr> a = args;
    add_implicit_vars(a, e);
    define_base_case(func, a, cast(e.type(), 1));
    (*this) = Expr(*this) * e;
}

void FuncRefExpr::operator-=(Expr e) {
    vector<Expr> a = args;
    add_implicit_vars(a, e);
    define_base_case(func, a, cast(e.type(), 0));
    (*this) = Expr(*this) - e;
}

void FuncRefExpr::operator/=(Expr e) {
    vector<Expr> a = args;
    add_implicit_vars(a, e);
    define_base_case(func, a, cast(e.type(), 1));
    (*this) = Expr(*this) / e;
}

FuncRefExpr::operator Expr() const {
    assert(func.value().defined() && "Can't call function with undefined value");
    return new Call(func, args);
}

Buffer Func::realize(int x_size, int y_size, int z_size, int w_size) {
    assert(value().defined() && "Can't realize undefined function");
    Type t = value().type();
    Buffer buf(t, x_size, y_size, z_size, w_size);
    realize(buf);
    return buf;
}

void Func::compile_to_bitcode(const string &filename, vector<Argument> args, const string &fn_name) {
    assert(value().defined() && "Can't compile undefined function");    

    Stmt stmt = Halide::Internal::lower(func);
    Argument me(name(), true, Int(1));
    args.push_back(me);

    StmtCompiler cg;
    cg.compile(stmt, fn_name.empty() ? name() : fn_name, args);
    cg.compile_to_bitcode(filename);
}

void Func::compile_to_object(const string &filename, vector<Argument> args, const string &fn_name) {
    assert(value().defined() && "Can't compile undefined function");    

    Stmt stmt = Halide::Internal::lower(func);
    Argument me(name(), true, Int(1));
    args.push_back(me);

    StmtCompiler cg;
    cg.compile(stmt, fn_name.empty() ? name() : fn_name, args);
    cg.compile_to_native(filename, false);
}

void Func::compile_to_header(const string &filename, vector<Argument> args, const string &fn_name) {    
    Argument me(name(), true, Int(1));
    args.push_back(me);

    ofstream header(filename.c_str());
    CodeGen_C cg(header);
    cg.compile_header(fn_name.empty() ? name() : fn_name, args);
}

void Func::compile_to_file(const string &filename_prefix, vector<Argument> args) {
    compile_to_header(filename_prefix + ".h", args, filename_prefix);
    compile_to_object(filename_prefix + ".o", args, filename_prefix);
}

void Func::compile_to_file(const string &filename_prefix) {
    compile_to_file(filename_prefix, vector<Argument>());
}

void Func::compile_to_file(const string &filename_prefix, Argument a) {
    compile_to_file(filename_prefix, Internal::vec(a));    
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b) {
    compile_to_file(filename_prefix, Internal::vec(a, b));    
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c) {
    compile_to_file(filename_prefix, Internal::vec(a, b, c));    
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c, Argument d) {
    compile_to_file(filename_prefix, Internal::vec(a, b, c, d));    
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c, Argument d, Argument e) {
    compile_to_file(filename_prefix, Internal::vec(a, b, c, d, e));    
}

void Func::compile_to_assembly(const string &filename, vector<Argument> args, const string &fn_name) {
    assert(value().defined() && "Can't compile undefined function");    

    Stmt stmt = Halide::Internal::lower(func);
    Argument me(name(), true, Int(1));
    args.push_back(me);

    StmtCompiler cg;
    cg.compile(stmt, fn_name.empty() ? name() : fn_name, args);
    cg.compile_to_native(filename, true);
}

void Func::set_error_handler(void (*handler)(char *)) {
    error_handler = handler;
    if (compiled_module.set_error_handler) {
        compiled_module.set_error_handler(handler);
    }
}

void Func::set_custom_allocator(void *(*cust_malloc)(size_t), void (*cust_free)(void *)) {
    custom_malloc = cust_malloc;
    custom_free = cust_free;
    if (compiled_module.set_custom_allocator) {
        compiled_module.set_custom_allocator(cust_malloc, cust_free);
    }
}

class InferArguments : public IRVisitor {
public:
    vector<Argument> arg_types;
    vector<const void *> arg_values;
    vector<pair<int, Internal::Parameter> > image_param_args;    

private:
    using IRVisitor::visit;

    void visit(const Load *op) {
        IRVisitor::visit(op);

        Buffer b;
        string arg_name;
        if (op->image.defined()) {            
            Internal::log(2) << "Found an image argument: " << op->image.name() << "\n";
            b = op->image;
            arg_name = op->image.name();
        } else if (op->param.defined()) {
            Internal::log(2) << "Found an image param argument: " << op->param.name() << "\n";
            b = op->param.get_buffer();
            arg_name = op->param.name();
        } else {
            return;
        }

        Argument arg(arg_name, true, Int(1));
        bool already_included = false;
        for (size_t i = 0; i < arg_types.size(); i++) {
            if (arg_types[i].name == op->name) {
                Internal::log(2) << "Already included.\n";
                already_included = true;
            }
        }
        if (!already_included) {
            if (op->param.defined()) {
                int idx = (int)(arg_values.size());
                image_param_args.push_back(make_pair(idx, op->param));
            }
            arg_types.push_back(arg);
            if (op->image.defined()) {
                assert(b.defined());
                arg_values.push_back(b.raw_buffer());
            } else {
                arg_values.push_back(NULL);
            }
        }
    }

    void visit(const Variable *op) {
        if (op->param.defined()) {
            Argument arg(op->param.name(), op->param.is_buffer(), op->param.type());
            bool already_included = false;
            for (size_t i = 0; i < arg_types.size(); i++) {
                if (arg_types[i].name == op->param.name()) {
                    already_included = true;
                }
            }
            if (!already_included) {
                Internal::log(2) << "Found a param: " << op->param.name() << "\n";
                if (op->param.is_buffer()) {
                    int idx = (int)(arg_values.size());
                    image_param_args.push_back(make_pair(idx, op->param));                    
                    arg_values.push_back(NULL);
                } else {
                    arg_values.push_back(op->param.get_scalar_address());
                }
                arg_types.push_back(arg);

            }            
        }
    }
};

void Func::realize(Buffer dst) {
    if (!compiled_module.wrapped_function) compile_jit();

    assert(compiled_module.wrapped_function);

    // Check the type and dimensionality of the buffer
    assert(dst.dimensions() == dimensions() && "Buffer and Func have different dimensionalities");
    assert(dst.type() == value().type() && "Buffer and Func have different element types");

    // In case these have changed since the last realization
    compiled_module.set_error_handler(error_handler);
    compiled_module.set_custom_allocator(custom_malloc, custom_free);   

    // Update the address of the buffer we're realizing into
    arg_values[arg_values.size()-1] = dst.raw_buffer();

    // Update the addresses of the image param args
    Internal::log(3) << image_param_args.size() << " image param args to set\n";
    for (size_t i = 0; i < image_param_args.size(); i++) {
        Internal::log(3) << "Updating address for image param: " << image_param_args[i].second.name() << "\n";
        Buffer b = image_param_args[i].second.get_buffer();
        assert(b.defined() && "An ImageParam is not bound to a buffer");
        arg_values[image_param_args[i].first] = b.raw_buffer();
    }

    for (size_t i = 0; i < arg_values.size(); i++) {
        Internal::log(2) << "Arg " << i << " = " << arg_values[i] << "\n";
        assert(arg_values[i] != NULL && "An argument to a jitted function is null\n");
    }

    Internal::log(2) << "Calling jitted function\n";
    compiled_module.wrapped_function(&(arg_values[0]));    
    Internal::log(2) << "Back from jitted function\n";
}

void Func::compile_jit() {
    assert(value().defined() && "Can't realize undefined function");
    
    Stmt stmt = Halide::Internal::lower(func);
    
    // Infer arguments
    InferArguments infer_args;
    stmt.accept(&infer_args);
    
    Argument me(name(), true, Int(1));
    infer_args.arg_types.push_back(me);
    arg_values = infer_args.arg_values;
    arg_values.push_back(NULL); // A spot to put the address of the output buffer
    image_param_args = infer_args.image_param_args;
    
    Internal::log(2) << "Inferred argument list:\n";
    for (size_t i = 0; i < infer_args.arg_types.size(); i++) {
        Internal::log(2) << infer_args.arg_types[i].name << ", " 
                         << infer_args.arg_types[i].type << ", " 
                         << infer_args.arg_types[i].is_buffer << "\n";
    }
    
    StmtCompiler cg;
    cg.compile(stmt, name(), infer_args.arg_types);
    
    if (log::debug_level >= 3) {
        cg.compile_to_native(name() + ".s", true);
        cg.compile_to_bitcode(name() + ".bc");
        ofstream stmt_debug((name() + ".stmt").c_str());
        stmt_debug << stmt;
    }
    
    compiled_module = cg.compile_to_function_pointers();    

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
