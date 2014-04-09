#include <algorithm>
#include <iostream>
#include <string.h>
#include <fstream>

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
#include "Debug.h"
#include "Target.h"

namespace Halide {

using std::max;
using std::min;
using std::make_pair;
using std::string;
using std::vector;
using std::pair;
using std::ofstream;

using namespace Internal;

Func::Func(const string &name) : func(unique_name(name)),
                                 error_handler(NULL),
                                 custom_malloc(NULL),
                                 custom_free(NULL),
                                 custom_do_par_for(NULL),
                                 custom_do_task(NULL),
                                 custom_trace(NULL),
                                 random_seed(0) {
}

Func::Func() : func(unique_name('f')),
               error_handler(NULL),
               custom_malloc(NULL),
               custom_free(NULL),
               custom_do_par_for(NULL),
               custom_do_task(NULL),
               custom_trace(NULL),
               random_seed(0) {
}

Func::Func(Expr e) : func(unique_name('f')),
                     error_handler(NULL),
                     custom_malloc(NULL),
                     custom_free(NULL),
                     custom_do_par_for(NULL),
                     custom_do_task(NULL),
                     custom_trace(NULL),
                     random_seed(0) {
    (*this)(_) = e;
}

Func::Func(Function f) : func(f),
                     error_handler(NULL),
                     custom_malloc(NULL),
                     custom_free(NULL),
                     custom_do_par_for(NULL),
                     custom_do_task(NULL),
                     custom_trace(NULL),
                     random_seed(0) {
}

/*
Func::Func(Buffer b) : func(unique_name('f')),
                       error_handler(NULL),
                       custom_malloc(NULL),
                       custom_free(NULL),
                       custom_do_par_for(NULL),
                       custom_do_task(NULL) {
    vector<Expr> args;
    for (int i = 0; i < b.dimensions(); i++) {
        args.push_back(Var::implicit(i));
    }
    (*this)(_) = Internal::Call::make(b, args);
}
*/

const string &Func::name() const {
    return func.name();
}

/** Get the pure arguments. */
std::vector<Var> Func::args() const {
    const std::vector<std::string> arg_names = func.args();
    std::vector<Var> args(arg_names.size());
    for (size_t i = 0; i < arg_names.size(); i++) {
        args[i] = Var(arg_names[i]);
    }
    return args;
}

/** The right-hand-side value of the pure definition of this
 * function. An error if the Func has no definition, or is defined as
 * a Tuple. */
Expr Func::value() const {
    assert(defined() &&
           "Can't call Func::value() on an undefined Func. To check if a Func is defined, call Func::defined()");
    assert(func.outputs() == 1 && "Can't call Func::value() on a func with multiple values");
    return func.values()[0];
}

/** The values returned by a Func, in Tuple form. */
Tuple Func::values() const {
    assert(defined() &&
           "Can't call Func::values() on an undefined Func. To check if a Func is defined, call Func::defined()");
    return Tuple(func.values());
}

/** Get the left-hand-side of the reduction definition. An empty
 * vector if there's no reduction definition. */
const std::vector<Expr> &Func::reduction_args(int idx) const {
    assert(is_reduction() && "Can't call Func::reduction_args() on a func with no reduction definition. "
           "Use Func::is_reduction() to check for the existence of a reduction definition\n");
    assert(idx < (int)func.reductions().size() && "Reduction index out of bounds\n");
    return func.reductions()[idx].args;
}

/** Get the right-hand-side of the reduction definition. An error if
 * there is no reduction definition. */
Expr Func::reduction_value(int idx) const {
    assert(is_reduction() && "Can't call Func::reduction_value() on a func with no reduction definition. "
           "Use Func::is_reduction() to check for the existence of a reduction definition\n");
    assert(idx < (int)func.reductions().size() && "Reduction index out of bounds\n");
    assert(func.reductions()[idx].values.size() == 1 &&
           "Can't call Func::reduction_value() on a func with multiple values");
    return func.reductions()[idx].values[0];
}

/** The reduction values returned by a Func, in Tuple form. */
Tuple Func::reduction_values(int idx) const {
    assert(is_reduction() && "Can't call Func::reduction_values() on a func with no reduction definition. "
           "Use Func::is_reduction() to check for the existence of a reduction definition\n");
    assert(idx < (int)func.reductions().size() && "Reduction index out of bounds\n");
    return Tuple(func.reductions()[idx].values);
}

/** Get the reduction domain for the reduction definition. Returns an
 * undefined RDom if there's no reduction definition, or if the
 * reduction definition has no domain. */
RDom Func::reduction_domain(int idx) const {
    assert(is_reduction() && "Can't call Func::reduction_domain() on a func with no reduction definition. "
           "Use Func::is_reduction() to check for the existence of a reduction definition\n");
    assert(idx < (int)func.reductions().size() && "Reduction index out of bounds\n");
    return func.reductions()[idx].domain;
}

bool Func::defined() const {
    return func.has_pure_definition() || func.has_extern_definition();
}

/** Is this function a reduction? */
bool Func::is_reduction() const {
    return func.has_reduction_definition();
}

/** How many reduction definitions are there? */
int Func::num_reduction_definitions() const {
    return (int)func.reductions().size();
}

/** Is this function external? */
EXPORT bool Func::is_extern() const {
    return func.has_extern_definition();
}

/** Add an extern definition for this Func. */
void Func::define_extern(const std::string &function_name,
                         const std::vector<ExternFuncArgument> &args,
                         const std::vector<Type> &types,
                         int dimensionality) {
    func.define_extern(function_name, args, types, dimensionality);
}

/** Get the types of the buffers returned by an extern definition. */
const std::vector<Type> &Func::output_types() const {
    return func.output_types();
}

/** Get the number of outputs this function has. */
int Func::outputs() const {
    return func.outputs();
}

/** Get the name of the extern function called for an extern
 * definition. */
EXPORT const std::string &Func::extern_function_name() const {
    return func.extern_function_name();
}

int Func::dimensions() const {
    if (!defined()) return 0;
    return func.dimensions();
}

FuncRefVar Func::operator()() const {
    // Bulk up the argument list using implicit vars
    vector<Var> args;
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x) const {
    // Bulk up the argument list using implicit vars
    vector<Var> args = vec(x);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x, Var y) const {
    vector<Var> args = vec(x, y);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x, Var y, Var z) const{
    vector<Var> args = vec(x, y, z);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x, Var y, Var z, Var w) const {
    vector<Var> args = vec(x, y, z, w);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x, Var y, Var z, Var w, Var u) const {
    vector<Var> args = vec(x, y, z, w, u);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x, Var y, Var z, Var w, Var u, Var v) const {
    vector<Var> args = vec(x, y, z, w, u, v);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(vector<Var> args) const {
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x) const {
    vector<Expr> args = vec(x);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x, Expr y) const {
    vector<Expr> args = vec(x, y);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z) const {
    vector<Expr> args = vec(x, y, z);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z, Expr w) const {
    vector<Expr> args = vec(x, y, z, w);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z, Expr w, Expr u) const {
    vector<Expr> args = vec(x, y, z, w, u);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(Expr x, Expr y, Expr z, Expr w, Expr u, Expr v) const {
    vector<Expr> args = vec(x, y, z, w, u, v);
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

FuncRefExpr Func::operator()(vector<Expr> args) const {
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefExpr(func, args, placeholder_pos);
}

int Func::add_implicit_vars(vector<Var> &args) const {
    int placeholder_pos = -1;
    std::vector<Var>::iterator iter = args.begin();

    while (iter != args.end() && !iter->same_as(_)) {
        iter++;
    }
    if (iter != args.end()) {
        placeholder_pos = (int)(iter - args.begin());
        int i = 0;
        iter = args.erase(iter);
        while ((int)args.size() < dimensions()) {
            Internal::debug(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
            iter = args.insert(iter, Var::implicit(i++));
            iter++;
        }
    }

    if (func.has_pure_definition() && args.size() != (size_t)dimensions()) {
        std::cerr << "Func " << name() << " was called with "
                  << args.size() << " arguments, but was defined with " << dimensions() << "\n";
        assert(false);
    }

    return placeholder_pos;
}

int Func::add_implicit_vars(vector<Expr> &args) const {
    int placeholder_pos = -1;
    std::vector<Expr>::iterator iter = args.begin();
    while (iter != args.end()) {
        const Variable *var = iter->as<Variable>();
        if (var && Var::is_implicit(var->name))
            break;
        iter++;
    }
    if (iter != args.end()) {
        placeholder_pos = (int)(iter - args.begin());
        int i = 0;
        iter = args.erase(iter);
        while ((int)args.size() < dimensions()) {
            Internal::debug(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
            iter = args.insert(iter, Var::implicit(i++));
            iter++;
        }
    }

    if (func.has_pure_definition() && args.size() != (size_t)dimensions()) {
        std::cerr << "Func " << name() << " was called with "
                  << args.size() << " arguments, but was defined with " << dimensions() << "\n";
        assert(false);
    }

    return placeholder_pos;
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
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].for_type = t;
        } else if (t == For::Vectorized) {
            assert(dims[i].for_type != For::Vectorized &&
                   "Can't vectorize across more than one variable");
        }
    }

    if (!found) {
        std::cerr << "Could not find dimension "
                  << var.name()
                  << " to mark as " << t
                  << " in argument list for function\n";
        dump_argument_list();
        assert(false);
    }
}

void ScheduleHandle::dump_argument_list() {
    std::cerr << "Argument list:";
    for (size_t i = 0; i < schedule.dims.size(); i++) {
        std::cerr << " " << schedule.dims[i].var;
    }
    std::cerr << "\n";
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

    if (!found) {
        std::cerr << "Could not find split dimension in argument list: "
                  << old.name()
                  << "\n";
        dump_argument_list();
        assert(false);
    }

    // Add the split to the splits list
    Schedule::Split split = {old_name, outer_name, inner_name, factor, Schedule::Split::SplitVar};
    schedule.splits.push_back(split);
    return *this;
}


ScheduleHandle &ScheduleHandle::fuse(Var inner, Var outer, Var fused) {
    // Replace the old dimensions with the new dimension in the dims list
    bool found_outer = false, found_inner = false;
    string inner_name, outer_name, fused_name;
    vector<Schedule::Dim> &dims = schedule.dims;

    for (size_t i = 0; (!found_outer) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, outer.name())) {
            found_outer = true;
            outer_name = dims[i].var;
            // Remove it
            for (size_t j = i; j < dims.size()-1; j++) {
                dims[j] = dims[j+1];
            }
            dims.pop_back();
        }
    }
    if (!found_outer) {
        std::cerr << "Could not find outer fuse dimension in argument list: "
                  << outer.name()
                  << "\n";
        dump_argument_list();
        assert(false);
    }

    for (size_t i = 0; (!found_inner) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, inner.name())) {
            found_inner = true;
            inner_name = dims[i].var;
            fused_name = inner_name + "." + fused.name();
            dims[i].var = fused_name;
        }
    }

    if (!found_inner) {
        std::cerr << "Could not find inner fuse dimension in argument list: "
                  << inner.name()
                  << "\n";
        dump_argument_list();
        assert(false);
    }


    // Add the fuse to the splits list
    Schedule::Split split = {fused_name, outer_name, inner_name, Expr(), Schedule::Split::FuseVars};
    schedule.splits.push_back(split);
    return *this;
}

ScheduleHandle &ScheduleHandle::rename(Var old_var, Var new_var) {
    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string old_name;
    vector<Schedule::Dim> &dims = schedule.dims;
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old_var.name())) {
            found = true;
            old_name = dims[i].var;
            dims[i].var += "." + new_var.name();
        }
    }

    string new_name = old_name + "." + new_var.name();

    if (!found) {
        std::cerr << "Could not find rename dimension in argument list: "
                  << old_var.name()
                  << "\n";
        dump_argument_list();
        assert(false);
    }

    if (old_name.find('.') == string::npos) {
        // If it's a primitive name, add the rename to the splits list.
        Schedule::Split split = {old_name, new_name, "", 1, Schedule::Split::RenameVar};
        schedule.splits.push_back(split);
    } else {
        // It's a derived name, so just rewrite the split or rename that defines it.
        bool found = false;
        for (size_t i = schedule.splits.size(); i > 0; i--) {
            if (schedule.splits[i-1].inner == old_name) {
                schedule.splits[i-1].inner = new_name;
                found = true;
                break;
            }
            if (schedule.splits[i-1].outer == old_name) {
                schedule.splits[i-1].outer = new_name;
                found = true;
                break;
            }
            if (schedule.splits[i-1].old_var == old_name) {
                assert(false && "Can't rename a variable that has already been renamed or split!");
            }
        }
        assert(found && "Rename failed: Old name is not an arg, and was not defined by a split.");
    }

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

ScheduleHandle &ScheduleHandle::parallel(Var var, Expr factor) {
    Var tmp;
    split(var, var, tmp, factor);
    parallel(var);
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

namespace {
// An helper function for reordering vars in a schedule.
ScheduleHandle &reorder_vars(ScheduleHandle& sched, const VarOrRVar *vars, size_t size) {
    if (size <= 1) {
        return sched;
    }
    if (size == 2) {
        return sched.reorder(vars[0], vars[1]);
    }
    for (size_t i = 1; i < size; i++) {
        sched.reorder(vars[0], vars[i]);
    }
    return reorder_vars(sched, vars + 1, size - 1);
}
}

ScheduleHandle &ScheduleHandle::reorder(const std::vector<VarOrRVar>& vars) {
    return reorder_vars(*this, &vars[0], vars.size());
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y) {
    vector<Schedule::Dim> &dims = schedule.dims;
    bool found_y = false;
    size_t y_loc = 0;
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, y.name())) {
            found_y = true;
            y_loc = i;
        } else if (var_name_match(dims[i].var, x.name())) {
            if (found_y) {
                std::swap(dims[i], dims[y_loc]);
            }
            return *this;
        }
    }
    assert(false && "Could not find these variables to reorder in schedule");
    return *this;
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z) {
    VarOrRVar vars[] = {x, y, z};
    return reorder_vars(*this, vars, 3);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w) {
    VarOrRVar vars[] = {x, y, z, w};
    return reorder_vars(*this, vars, 4);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t) {
    VarOrRVar vars[] = {x, y, z, w, t};
    return reorder_vars(*this, vars, 5);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2};
    return reorder_vars(*this, vars, 6);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3};
    return reorder_vars(*this, vars, 7);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4};
    return reorder_vars(*this, vars, 8);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4, VarOrRVar t5) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4, t5};
    return reorder_vars(*this, vars, 9);
}

ScheduleHandle &ScheduleHandle::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4, VarOrRVar t5, VarOrRVar t6) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4, t5, t6};
    return reorder_vars(*this, vars, 10);
}

ScheduleHandle &ScheduleHandle::gpu_threads(Var tx, GPUAPI /* gpu_api */) {
    parallel(tx);
    rename(tx, Var("threadidx"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_threads(Var tx, Var ty, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    rename(tx, Var("threadidx"));
    rename(ty, Var("threadidy"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_threads(Var tx, Var ty, Var tz, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, Var("threadidx"));
    rename(ty, Var("threadidy"));
    rename(tz, Var("threadidz"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_blocks(Var tx, GPUAPI /* gpu_api */) {
    parallel(tx);
    rename(tx, Var("blockidx"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_blocks(Var tx, Var ty, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    rename(tx, Var("blockidx"));
    rename(ty, Var("blockidy"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_blocks(Var tx, Var ty, Var tz, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, Var("blockidx"));
    rename(ty, Var("blockidy"));
    rename(tz, Var("blockidz"));
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu(Var bx, Var tx, GPUAPI /* gpu_api */) {
    return gpu_blocks(bx).gpu_threads(tx);
}

ScheduleHandle &ScheduleHandle::gpu(Var bx, Var by,
                                    Var tx, Var ty, GPUAPI /* gpu_api */) {
    return gpu_blocks(bx, by).gpu_threads(tx, ty);
}

ScheduleHandle &ScheduleHandle::gpu(Var bx, Var by, Var bz,
                                    Var tx, Var ty, Var tz,
				    GPUAPI /* gpu_api */) {
    return gpu_blocks(bx, by, bz).gpu_threads(tx, ty, tz);
}

ScheduleHandle &ScheduleHandle::gpu_tile(Var x, Expr x_size, GPUAPI /* gpu_api */) {
    Var bx("blockidx"), tx("threadidx");
    split(x, bx, tx, x_size);
    parallel(bx);
    parallel(tx);
    return *this;
}


ScheduleHandle &ScheduleHandle::gpu_tile(Var x, Var y,
                                         Expr x_size, Expr y_size,
					 GPUAPI /* gpu_api */) {
    Var bx("blockidx"), by("blockidy"), tx("threadidx"), ty("threadidy");
    tile(x, y, bx, by, tx, ty, x_size, y_size);
    parallel(bx);
    parallel(by);
    parallel(tx);
    parallel(ty);
    return *this;
}

ScheduleHandle &ScheduleHandle::gpu_tile(Var x, Var y, Var z,
                                         Expr x_size, Expr y_size, Expr z_size,
					 GPUAPI /* gpu_api */) {
    Var bx("blockidx"), by("blockidy"), bz("blockidz"),
        tx("threadidx"), ty("threadidy"), tz("threadidz");
    split(x, bx, tx, x_size);
    split(y, by, ty, y_size);
    split(z, bz, tz, z_size);
    // current order is:
    // tx bx ty by tz bz
    reorder(ty, bx);
    // tx ty bx by tz bz
    reorder(tz, bx);
    // tx ty tz by bx bz
    reorder(bx, by);
    // tx ty tz bx by bz
    parallel(bx);
    parallel(by);
    parallel(bz);
    parallel(tx);
    parallel(ty);
    parallel(tz);
    return *this;
}

Func &Func::split(Var old, Var outer, Var inner, Expr factor) {
    ScheduleHandle(func.schedule()).split(old, outer, inner, factor);
    return *this;
}

Func &Func::fuse(Var inner, Var outer, Var fused) {
    ScheduleHandle(func.schedule()).fuse(inner, outer, fused);
    return *this;
}

Func &Func::rename(Var old_name, Var new_name) {
    ScheduleHandle(func.schedule()).rename(old_name, new_name);
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

Func &Func::parallel(Var var, Expr factor) {
    ScheduleHandle(func.schedule()).parallel(var, factor);
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
    bool found = false;
    for (size_t i = 0; i < func.args().size(); i++) {
        if (var.name() == func.args()[i]) {
            found = true;
        }
    }
    if (!found) {
        std::cerr << "Can't bound variable " << var.name()
                  << " of function " << name()
                  << " because " << var.name()
                  << " is not one of the pure variables of " << name() << "\n";
        assert(false);
    }

    Schedule::Bound b = {var.name(), min, extent};
    func.schedule().bounds.push_back(b);
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

Func &Func::reorder(const std::vector<VarOrRVar> &vars) {
    ScheduleHandle(func.schedule()).reorder(vars);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y) {
    ScheduleHandle(func.schedule()).reorder(x, y);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z) {
    ScheduleHandle(func.schedule()).reorder(x, y, z);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t1, t2);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t1, t2, t3);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t1, t2, t3, t4);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4,
                    VarOrRVar t5) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t1, t2, t3, t4, t5);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4,
                    VarOrRVar t5, VarOrRVar t6) {
    ScheduleHandle(func.schedule()).reorder(x, y, z, w, t1, t2, t3, t4, t5, t6);
    return *this;
}

Func &Func::gpu_threads(Var tx, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_threads(tx, gpuapi);
    return *this;
}

Func &Func::gpu_threads(Var tx, Var ty, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_threads(tx, ty, gpuapi);
    return *this;
}

Func &Func::gpu_threads(Var tx, Var ty, Var tz, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_threads(tx, ty, tz, gpuapi);
    return *this;
}

  Func &Func::gpu_blocks(Var bx, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_blocks(bx, gpuapi);
    return *this;
}

Func &Func::gpu_blocks(Var bx, Var by, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_blocks(bx, by, gpuapi);
    return *this;
}

Func &Func::gpu_blocks(Var bx, Var by, Var bz, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_blocks(bx, by, bz, gpuapi);
    return *this;
}

Func &Func::gpu(Var bx, Var tx, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu(bx, tx, gpuapi);
    return *this;
}

Func &Func::gpu(Var bx, Var by, Var tx, Var ty, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu(bx, by, tx, ty, gpuapi);
    return *this;
}

Func &Func::gpu(Var bx, Var by, Var bz, Var tx, Var ty, Var tz, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu(bx, by, bz, tx, ty, tz, gpuapi);
    return *this;
}

  Func &Func::gpu_tile(Var x, int x_size, GPUAPI gpuapi) {
    ScheduleHandle(func.schedule()).gpu_tile(x, x_size, gpuapi);
    return *this;
}

  Func &Func::gpu_tile(Var x, Var y, int x_size, int y_size, GPUAPI gpuapi) {
    ScheduleHandle(func.schedule()).gpu_tile(x, y, x_size, y_size, gpuapi);
    return *this;
}

Func &Func::gpu_tile(Var x, Var y, Var z, int x_size, int y_size, int z_size, GPUAPI gpuapi) {
  ScheduleHandle(func.schedule()).gpu_tile(x, y, z, x_size, y_size, z_size, gpuapi);
    return *this;
}

Func &Func::reorder_storage(Var x, Var y) {
    vector<string> &dims = func.schedule().storage_dims;
    bool found_y = false;
    size_t y_loc = 0;
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i], y.name())) {
            found_y = true;
            y_loc = i;
        } else if (var_name_match(dims[i], x.name())) {
            if (found_y) std::swap(dims[i], dims[y_loc]);
            return *this;
        }
    }
    assert(false && "Could not find these variables to reorder in schedule");
    return *this;
}

Func &Func::reorder_storage(Var x, Var y, Var z) {
    reorder_storage(x, y);
    reorder_storage(x, z);
    reorder_storage(y, z);
    return *this;
}

Func &Func::reorder_storage(Var x, Var y, Var z, Var w) {
    reorder_storage(x, y);
    reorder_storage(x, z);
    reorder_storage(x, w);
    reorder_storage(y, z, w);
    return *this;
}

Func &Func::reorder_storage(Var x, Var y, Var z, Var w, Var t) {
    reorder_storage(x, y);
    reorder_storage(x, z);
    reorder_storage(x, w);
    reorder_storage(x, t);
    reorder_storage(y, z, w, t);
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
    if (func.schedule().store_level.is_inline()) {
        func.schedule().store_level = Schedule::LoopLevel::root();
    }
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

Func &Func::trace_loads() {
    func.trace_loads();
    return *this;
}

Func &Func::trace_stores() {
    func.trace_stores();
    return *this;
}

Func &Func::trace_realizations() {
    func.trace_realizations();
    return *this;
}

void Func::debug_to_file(const string &filename) {
    func.debug_file() = filename;
}

ScheduleHandle Func::update(int idx) {
    return ScheduleHandle(func.reduction_schedule(idx));
}

FuncRefVar::FuncRefVar(Internal::Function f, const vector<Var> &a, int placeholder_pos) : func(f) {
    implicit_placeholder_pos = placeholder_pos;
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = a[i].name();
    }
}

namespace {
class CountImplicitVars : public Internal::IRGraphVisitor {
public:
    int count;

    CountImplicitVars(const vector<Expr> &e) : count(0) {
        for (size_t i = 0; i < e.size(); i++) {
            e[i].accept(this);
        }
    }

    using IRGraphVisitor::visit;

    void visit(const Variable *v) {
      int index = Var::implicit_index(v->name);
      if (index != -1) {
            if (index >= count) count = index + 1;
      }
    }
};
}

vector<string> FuncRefVar::args_with_implicit_vars(const vector<Expr> &e) const {
    vector<string> a = args;

    CountImplicitVars count(e);

    if (count.count > 0) {
        if (implicit_placeholder_pos != -1) {
            Internal::debug(2) << "Adding " << count.count << " implicit vars to LHS of " <<
                func.name() << " at position " << implicit_placeholder_pos << "\n";

            vector<std::string>::iterator iter = a.begin() + implicit_placeholder_pos;
            for (int i = 0; i < count.count; i++) {
                iter = a.insert(iter, Var::implicit(i).name());
                iter++;
            }
        }
    }

    // Check the implicit vars in the RHS also exist in the LHS
    for (int i = 0; i < count.count; i++) {
        Var v = Var::implicit(i);
        bool found = false;
        for (size_t j = 0; j < a.size(); j++) {
            if (a[j] == v.name()) {
                found = true;
            }
        }
        if (!found) {
            std::cerr << "Right-hand-side of pure definition of " << func.name()
                      << " uses implicit variables, but the left-hand-side does not"
                      << " contain the placeholder symbol '_'. This behavior has been deprecated.\n";
            assert(false);
        }
    }

    return a;
}

void FuncRefVar::operator=(Expr e) {
    (*this) = Tuple(vec<Expr>(e));
}

void FuncRefVar::operator=(const Tuple &e) {
    // If the function has already been defined, this must actually be a reduction
    if (func.has_pure_definition()) {
        FuncRefExpr(func, args) = e;
        return;
    }

    // Find implicit args in the expr and add them to the args list before calling define
    vector<string> a = args_with_implicit_vars(e.as_vector());
    func.define(a, e.as_vector());
}

void FuncRefVar::operator=(const FuncRefVar &e) {
    if (e.size() == 1) {
        (*this) = Expr(e);
    } else {
        (*this) = Tuple(e);
    }
}

void FuncRefVar::operator=(const FuncRefExpr &e) {
    if (e.size() == 1) {
        (*this) = Expr(e);
    } else {
        (*this) = Tuple(e);
    }
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
    assert((func.has_pure_definition() || func.has_extern_definition()) && "Can't call undefined function");
    vector<Expr> expr_args(args.size());
    for (size_t i = 0; i < expr_args.size(); i++) {
        expr_args[i] = Var(args[i]);
    }
    assert(func.outputs() == 1 &&
           "Can't convert a reference to a function that has multiple outputs to an Expr");
    return Call::make(func, expr_args);
}

Expr FuncRefVar::operator[](int i) const {
    assert((func.has_pure_definition() || func.has_extern_definition()) && "Can't call undefined function");
    assert(func.outputs() != 1 &&
           "Can't index into a reference to a function that only provides one output");
    assert(i >= 0 && i < func.outputs() && "index out of range");
    vector<Expr> expr_args(args.size());
    for (size_t j = 0; j < expr_args.size(); j++) {
        expr_args[j] = Var(args[j]);
    }
    return Call::make(func, expr_args, i);
}

size_t FuncRefVar::size() const {
    return func.outputs();
}

/*
FuncRefVar::operator Tuple() const {
    assert(func.has_pure_definition() && "Can't call undefined function");
    assert(func.outputs() != 1 &&
           "Can't create a tuple from a call to a function that only provides one output");
    vector<Expr> expr_args(args.size());
    for (size_t j = 0; j < expr_args.size(); j++) {
        expr_args[j] = Var(args[j]);
    }
    Tuple tuple(std::vector<Expr>(func.outputs()));
    for (size_t i = 0; i < tuple.size(); i++) {
        tuple[i] = Call::make(func, expr_args, i);
    }
    return tuple;
}
*/

FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<Expr> &a, int placeholder_pos) : func(f), args(a) {
    implicit_placeholder_pos = placeholder_pos;
    ImageParam::check_arg_types(f.name(), &args);
}

FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<string> &a,
                         int placeholder_pos) : func(f) {
    implicit_placeholder_pos = placeholder_pos;
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = Var(a[i]);
    }
}

vector<Expr> FuncRefExpr::args_with_implicit_vars(const vector<Expr> &e) const {
    vector<Expr> a = args;

    CountImplicitVars count(e);
    // TODO: Check if there is a test case for this and add one if not.
    // Implicit vars are also allowed in the lhs of a reduction. E.g.:
    // f(x, y, z) = x+y
    // g(x, y, z) = 0
    // g(f(r.x, _), _) = 1   (this means g(f(r.x, _0, _1), _0, _1) = 1)

    for (size_t i = 0; i < a.size(); i++) {
        a[i].accept(&count);
    }

    if (count.count > 0) {
        if (implicit_placeholder_pos != -1) {
            Internal::debug(2) << "Adding " << count.count << " implicit vars to LHS of " << func.name() << "\n";

            vector<Expr>::iterator iter = a.begin() + implicit_placeholder_pos;
            for (int i = 0; i < count.count; i++) {
                iter = a.insert(iter, Var::implicit(i));
                iter++;
            }
        }
    }

    // Check the implicit vars in the RHS also exist in the LHS
    for (int i = 0; i < count.count; i++) {
        Var v = Var::implicit(i);
        bool found = false;
        for (size_t j = 0; j < a.size(); j++) {
            if (const Variable *arg = a[j].as<Variable>()) {
                if (arg->name == v.name()) {
                    found = true;
                }
            }
        }
        if (!found) {
            std::cerr << "Right-hand-side of update definition of " << func.name()
                      << " uses implicit variables, but the left-hand-side does not"
                      << " contain the placeholder symbol '_'. This behavior has been deprecated.\n";
            assert(false);
        }
    }

    return a;
}

void FuncRefExpr::operator=(Expr e) {
    (*this) = Tuple(vec<Expr>(e));
}

void FuncRefExpr::operator=(const Tuple &e) {
    assert(func.has_pure_definition() &&
           "Can't add a reduction definition to an undefined function");

    vector<Expr> a = args_with_implicit_vars(e.as_vector());
    func.define_reduction(args, e.as_vector());
}

void FuncRefExpr::operator=(const FuncRefExpr &e) {
    if (e.size() == 1) {
        (*this) = Expr(e);
    } else {
        (*this) = Tuple(e);
    }
}

void FuncRefExpr::operator=(const FuncRefVar &e) {
    if (e.size() == 1) {
        (*this) = Expr(e);
    } else {
        (*this) = Tuple(e);
    }
}

// Inject a suitable base-case definition given a reduction
// definition. This is a helper for FuncRefExpr::operator+= and co.
void define_base_case(Internal::Function func, const vector<Expr> &a, Expr e) {
    if (func.has_pure_definition()) return;
    vector<Var> pure_args(a.size());

    // Reuse names of existing pure args
    for (size_t i = 0; i < a.size(); i++) {
        if (const Variable *v = a[i].as<Variable>()) {
            if (!v->param.defined()) {
                pure_args[i] = Var(v->name);
            }
        } else {
            pure_args[i] = Var();
        }
    }

    FuncRefVar(func, pure_args) = e;
}

void FuncRefExpr::operator+=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 0));
    (*this) = Expr(*this) + e;
}

void FuncRefExpr::operator*=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 1));
    (*this) = Expr(*this) * e;
}

void FuncRefExpr::operator-=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 0));
    (*this) = Expr(*this) - e;
}

void FuncRefExpr::operator/=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 1));
    (*this) = Expr(*this) / e;
}

FuncRefExpr::operator Expr() const {
    assert((func.has_pure_definition() || func.has_extern_definition()) && "Can't call undefined function");
    assert(func.outputs() == 1 &&
           "Can't convert a reference to a function that has multiple outputs to an Expr");
    return Call::make(func, args);
}

Expr FuncRefExpr::operator[](int i) const {
    assert((func.has_pure_definition() || func.has_extern_definition()) && "Can't call undefined function");
    if (func.has_pure_definition()) {
        assert(func.outputs() != 1 &&
               "Can't index into a reference to a function that only provides one output");
    } else {

    }
    assert(i >= 0 && i < func.outputs() && "index out of range");
    return Call::make(func, args, i);
}

size_t FuncRefExpr::size() const {
    return func.outputs();
}

/*
FuncRefExpr::operator Tuple() const {
    assert(func.has_pure_definition() && "Can't call undefined function");
    assert(func.outputs() != 1 &&
           "Can't create a tuple from a call to a function that only provides one output");
    Tuple tuple(std::vector<Expr>(func.outputs()));
    for (size_t i = 0; i < tuple.size(); i++) {
        tuple[i] = Call::make(func, args, i);
    }
    return tuple;
}
*/

Realization Func::realize(std::vector<int32_t> sizes, const Target &target) {
    assert(defined() && "Can't realize undefined function");
    vector<Buffer> outputs(func.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(func.output_types()[i], sizes);
    }
    Realization r(outputs);
    realize(r, target);
    return r;
}

Realization Func::realize(int x_size, int y_size, int z_size, int w_size, const Target &target) {
    assert(defined() && "Can't realize undefined function");
    vector<Buffer> outputs(func.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(func.output_types()[i], x_size, y_size, z_size, w_size);
    }
    Realization r(outputs);
    realize(r, target);
    return r;
}

Realization Func::realize(int x_size, int y_size, int z_size, const Target &target) {
    return realize(x_size, y_size, z_size, 0, target);
}

Realization Func::realize(int x_size, int y_size, const Target &target) {
    return realize(x_size, y_size, 0, 0, target);
}

Realization Func::realize(int x_size, const Target &target) {
    return realize(x_size, 0, 0, 0, target);
}

void Func::infer_input_bounds(int x_size, int y_size, int z_size, int w_size) {
    assert(defined() && "Can't infer input bounds on an undefined function");
    vector<Buffer> outputs(func.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(func.output_types()[i], x_size, y_size, z_size, w_size, (uint8_t *)1);
    }
    Realization r(outputs);
    infer_input_bounds(r);
}

OutputImageParam Func::output_buffer() const {
    assert(defined() && "Can't access output buffer of undefined function");
    assert(func.output_buffers().size() == 1 && "Can only call Func::output_buffer on Funcs with one value");
    return OutputImageParam(func.output_buffers()[0], dimensions());
}

vector<OutputImageParam> Func::output_buffers() const {
    assert(defined() && "Can't access output buffers of undefined function");

    vector<OutputImageParam> bufs(func.output_buffers().size());
    for (size_t i = 0; i < bufs.size(); i++) {
        bufs[i] = OutputImageParam(func.output_buffers()[i], dimensions());
    }
    return bufs;
}

namespace {

class InferArguments : public IRGraphVisitor {
public:
    vector<Argument> arg_types;
    vector<const void *> arg_values;
    vector<pair<int, Internal::Parameter> > image_param_args;
    vector<pair<int, Buffer> > image_args;

    InferArguments(const string &o) : output(o) {}

private:
    const string &output;

    using IRGraphVisitor::visit;

    bool already_have(const string &name) {
        // Ignore dependencies on the output buffers
        if (name == output || starts_with(name, output + ".")) {
            return true;
        }
        for (size_t i = 0; i < arg_types.size(); i++) {
            if (arg_types[i].name == name) {
                return true;
            }
        }
        return false;
    }

    void include_parameter(Internal::Parameter p) {
        if (!p.defined()) return;
        if (already_have(p.name())) return;
        arg_types.push_back(Argument(p.name(), p.is_buffer(), p.type()));
        if (p.is_buffer()) {
            Buffer b = p.get_buffer();
            int idx = (int)arg_values.size();
            image_param_args.push_back(make_pair(idx, p));
            if (b.defined()) {
                arg_values.push_back(b.raw_buffer());
            } else {
                arg_values.push_back(NULL);
            }
        } else {
            arg_values.push_back(p.get_scalar_address());
        }
    }

    void include_buffer(Buffer b) {
        if (!b.defined()) return;
        if (already_have(b.name())) return;
        image_args.push_back(make_pair((int)arg_types.size(), b));
        arg_types.push_back(Argument(b.name(), true, b.type()));
        arg_values.push_back(b.raw_buffer());
    }

    void visit(const Load *op) {
        IRGraphVisitor::visit(op);

        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Variable *op) {
        include_parameter(op->param);
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);

        // Sometimes, a buffer will be referred to by an intrinsic and nowhere else.
        if (op->call_type == Call::Intrinsic) {
            include_buffer(op->image);
            include_parameter(op->param);
        }
    }
};

/** Check that all the necessary arguments are in an args vector. Any
 * images in the source that aren't in the args vector are placed in
 * the images_to_embed list. */
void validate_arguments(const string &output,
                        const vector<Argument> &args,
                        Stmt lowered,
                        vector<Buffer> &images_to_embed) {
    InferArguments infer_args(output);
    lowered.accept(&infer_args);
    const vector<Argument> &required_args = infer_args.arg_types;

    for (size_t i = 0; i < required_args.size(); i++) {
        const Argument &arg = required_args[i];

        Buffer buf;
        for (size_t j = 0; !buf.defined() && j < infer_args.image_args.size(); j++) {
            if (infer_args.image_args[j].first == (int)i) {
                buf = infer_args.image_args[j].second;
                assert(buf.defined());
            }
        }

        bool found = false;
        for (size_t j = 0; !found && j < args.size(); j++) {
            if (args[j].name == arg.name) {
                found = true;
            }
        }

        if (buf.defined() && !found) {
            // It's a raw Buffer used that isn't in the args
            // list. Embed it in the output instead.
            images_to_embed.push_back(buf);
            Internal::debug(1) << "Embedding image " << buf.name() << "\n";
        } else if (!found) {
            std::cerr << "Generated code refers to ";
            if (arg.is_buffer) std::cerr << "image ";
            std::cerr << "parameter " << arg.name
                      << ", which was not found in the argument list\n";

            std::cerr << "\nArgument list specified: ";
            for (size_t i = 0; i < args.size(); i++) {
                std::cerr << args[i].name << " ";
            }
            std::cerr << "\n\nParameters referenced in generated code: ";
            for (size_t i = 0; i < required_args.size(); i++) {
                std::cerr << required_args[i].name << " ";
            }
            std::cerr << "\n\n";
            assert(false);
        }
    }
}
}


  void Func::compile_to_bitcode(const string &filename, vector<Argument> args, const string &fn_name,
                                const Target &target) {
    assert(defined() && "Can't compile undefined function");

    if (!lowered.defined()) {
        lowered = Halide::Internal::lower(func);
    }

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    StmtCompiler cg(target);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);
    cg.compile_to_bitcode(filename);
}

void Func::compile_to_bitcode(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_bitcode(filename, args, "", target);
}

  void Func::compile_to_object(const string &filename, vector<Argument> args, const string &fn_name,
                               const Target &target) {
    assert(defined() && "Can't compile undefined function");

    if (!lowered.defined()) {
        lowered = Halide::Internal::lower(func);
    }

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    StmtCompiler cg(target);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);
    cg.compile_to_native(filename, false);
}

void Func::compile_to_object(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_object(filename, args, "", target);
}

void Func::compile_to_header(const string &filename, vector<Argument> args, const string &fn_name) {
    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    ofstream header(filename.c_str());
    CodeGen_C cg(header);
    cg.compile_header(fn_name.empty() ? name() : fn_name, args);
}

void Func::compile_to_c(const string &filename, vector<Argument> args, const string &fn_name) {
    if (!lowered.defined()) {
        lowered = Halide::Internal::lower(func);
    }

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    ofstream src(filename.c_str());
    CodeGen_C cg(src);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);
}

void Func::compile_to_lowered_stmt(const string &filename) {
    if (!lowered.defined()) {
        lowered = Halide::Internal::lower(func);
    }

    ofstream stmt_output(filename.c_str());
    stmt_output << lowered;
}

void Func::compile_to_file(const string &filename_prefix, vector<Argument> args,
                           const Target &target) {
    compile_to_header(filename_prefix + ".h", args, filename_prefix);
    compile_to_object(filename_prefix + ".o", args, filename_prefix, target);
}

void Func::compile_to_file(const string &filename_prefix, const Target &target) {
  compile_to_file(filename_prefix, vector<Argument>(), target);
}

void Func::compile_to_file(const string &filename_prefix, Argument a,
                           const Target &target) {
  compile_to_file(filename_prefix, Internal::vec(a), target);
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b,
                           const Target &target) {
  compile_to_file(filename_prefix, Internal::vec(a, b), target);
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c,
                           const Target &target) {
  compile_to_file(filename_prefix, Internal::vec(a, b, c), target);
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c, Argument d,
                           const Target &target) {
  compile_to_file(filename_prefix, Internal::vec(a, b, c, d), target);
}

void Func::compile_to_file(const string &filename_prefix, Argument a, Argument b, Argument c, Argument d, Argument e,
                           const Target &target) {
  compile_to_file(filename_prefix, Internal::vec(a, b, c, d, e), target);
}

void Func::compile_to_assembly(const string &filename, vector<Argument> args, const string &fn_name,
                               const Target &target) {
    assert(defined() && "Can't compile undefined function");

    if (!lowered.defined()) lowered = Halide::Internal::lower(func);

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    StmtCompiler cg(target);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);
    cg.compile_to_native(filename, true);
}

void Func::compile_to_assembly(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_assembly(filename, args, "", target);
}

void Func::set_error_handler(void (*handler)(void *, const char *)) {
    error_handler = handler;
    if (compiled_module.set_error_handler) {
        compiled_module.set_error_handler(handler);
    }
}

void Func::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                void (*cust_free)(void *, void *)) {
    custom_malloc = cust_malloc;
    custom_free = cust_free;
    if (compiled_module.set_custom_allocator) {
        compiled_module.set_custom_allocator(cust_malloc, cust_free);
    }
}

void Func::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    custom_do_par_for = cust_do_par_for;
    if (compiled_module.set_custom_do_par_for) {
        compiled_module.set_custom_do_par_for(cust_do_par_for);
    }
}

void Func::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    custom_do_task = cust_do_task;
    if (compiled_module.set_custom_do_task) {
        compiled_module.set_custom_do_task(cust_do_task);
    }
}

void Func::set_custom_trace(Internal::JITCompiledModule::TraceFn t) {
    custom_trace = t;
    if (compiled_module.set_custom_trace) {
        compiled_module.set_custom_trace(t);
    }
}

void Func::realize(Buffer b, const Target &target) {
    realize(Realization(vec<Buffer>(b)), target);
}

void Func::realize(Realization dst, const Target &target) {
    if (!compiled_module.wrapped_function) compile_jit(target);

    assert(compiled_module.wrapped_function);

    // Check the type and dimensionality of the buffer
    for (size_t i = 0; i < dst.size(); i++) {
        assert(dst[i].dimensions() == dimensions() && "Buffer and Func have different dimensionalities");
        assert(dst[i].type() == func.output_types()[i] && "Buffer and Func have different element types");
    }

    // In case these have changed since the last realization
    compiled_module.set_error_handler(error_handler);
    compiled_module.set_custom_allocator(custom_malloc, custom_free);
    compiled_module.set_custom_do_par_for(custom_do_par_for);
    compiled_module.set_custom_do_task(custom_do_task);
    compiled_module.set_custom_trace(custom_trace);

    // Update the address of the buffers we're realizing into
    for (size_t i = 0; i < dst.size(); i++) {
        arg_values[arg_values.size()-dst.size()+i] = dst[i].raw_buffer();
    }

    // Update the addresses of the image param args
    Internal::debug(3) << image_param_args.size() << " image param args to set\n";
    for (size_t i = 0; i < image_param_args.size(); i++) {
        Internal::debug(3) << "Updating address for image param: " << image_param_args[i].second.name() << "\n";
        Buffer b = image_param_args[i].second.get_buffer();
        assert(b.defined() && "An ImageParam is not bound to a buffer");
        buffer_t *buf = b.raw_buffer();
        arg_values[image_param_args[i].first] = buf;
        assert((buf->host || buf->dev) && "An ImageParam is bound to a buffer with NULL host and dev pointers");
    }

    for (size_t i = 0; i < arg_values.size(); i++) {
        Internal::debug(2) << "Arg " << i << " = " << arg_values[i] << "\n";
        assert(arg_values[i] && "An argument to a jitted function is null\n");
    }

    Internal::debug(2) << "Calling jitted function\n";
    int exit_status = compiled_module.wrapped_function(&(arg_values[0]));
    Internal::debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    for (size_t i = 0; i < dst.size(); i++) {
        dst[i].set_source_module(compiled_module);
    }
}

void Func::infer_input_bounds(Buffer dst) {
    Realization r = Realization(vec<Buffer>(dst));
    infer_input_bounds(r, 1);
    assert(r[0].raw_buffer() == dst.raw_buffer());
}

static Buffer allocate_dummy_buffer(buffer_t buf, Type ty) {
    // Figure out how much memory to allocate for this buffer
    size_t min_idx = 0, max_idx = 0;
    for (int d = 0; d < 4; d++) {
        if (buf.stride[d] > 0) {
            min_idx += buf.min[d] * buf.stride[d];
            max_idx += (buf.min[d] + buf.extent[d] - 1) * buf.stride[d];
        } else {
            max_idx += buf.min[d] * buf.stride[d];
            min_idx += (buf.min[d] + buf.extent[d] - 1) * buf.stride[d];
        }
    }
    size_t total_size = (max_idx - min_idx);
    while (total_size & 0x1f) total_size++;

    // Allocate enough memory with the right dimensionality.
    Buffer buffer(ty, total_size,
                  buf.extent[1] > 0 ? 1 : 0,
                  buf.extent[2] > 0 ? 1 : 0,
                  buf.extent[3] > 0 ? 1 : 0);

    // Rewrite the buffer fields to match the ones returned
    for (int d = 0; d < 4; d++) {
        buffer.raw_buffer()->min[d] = buf.min[d];
        buffer.raw_buffer()->stride[d] = buf.stride[d];
        buffer.raw_buffer()->extent[d] = buf.extent[d];
    }

    return buffer;
}

void Func::infer_input_bounds(Realization &dst, int max_iterations) {
    if (!compiled_module.wrapped_function) compile_jit();

    assert(compiled_module.wrapped_function);


    // Check the type and dimensionality of the buffer
    for (size_t i = 0; i < dst.size(); i++) {
        assert(dst[i].dimensions() == dimensions() && "Buffer and Func have different dimensionalities");
        assert(dst[i].type() == func.output_types()[i] && "Buffer and Func have different element types");
    }

    // In case these have changed since the last realization
    compiled_module.set_error_handler(error_handler);
    compiled_module.set_custom_allocator(custom_malloc, custom_free);
    compiled_module.set_custom_do_par_for(custom_do_par_for);
    compiled_module.set_custom_do_task(custom_do_task);
    compiled_module.set_custom_trace(custom_trace);

    // Update the address of the buffers we're realizing into
    for (size_t i = 0; i < dst.size(); i++) {
        arg_values[arg_values.size()-dst.size()+i] = dst[i].raw_buffer();
    }

    vector<buffer_t> dummy_buffers;
    for (int it = 0; it < max_iterations; it++) {
        Internal::debug(1) << "infer_input_bounds, iteration " << it << "\n";

        // Track the previous output mins/extents for potential changes
        std::vector<std::vector<size_t> > prev_mins(dst.size()), prev_extents(dst.size());
        for (int i = 0; i < dst.size(); i++) {
            for (int d = 0; d < dst[i].dimensions(); d++) {
                prev_mins[i].push_back(dst[i].min(d));
                prev_extents[i].push_back(dst[i].extent(d));
            }
        }

        // Update the addresses of the image param args
        Internal::debug(3) << image_param_args.size() << " image param args to set\n";

        dummy_buffers.clear();
        for (size_t i = 0; i < image_param_args.size(); i++) {
            Internal::debug(3) << "Updating address for image param: " << image_param_args[i].second.name() << "\n";
            Buffer b = image_param_args[i].second.get_buffer();
            if (b.defined()) {
                arg_values[image_param_args[i].first] = b.raw_buffer();
            } else {
                Internal::debug(1) << "Going to infer input size for param " << image_param_args[i].second.name() << "\n";
                buffer_t buf;
                memset(&buf, 0, sizeof(buffer_t));
                dummy_buffers.push_back(buf);
                arg_values[image_param_args[i].first] = &dummy_buffers[dummy_buffers.size()-1];
            }
        }

        for (size_t i = 0; i < arg_values.size(); i++) {
            Internal::debug(2) << "Arg " << i << " = " << arg_values[i] << "\n";
            assert(arg_values[i] && "An argument to a jitted function is null\n");
        }

        Internal::debug(2) << "Calling jitted function\n";
        int exit_status = compiled_module.wrapped_function(&(arg_values[0]));
        if (exit_status) {
            std::cerr << "Calling " << name()
                      << " in bounds inference mode returned non-success ("
                      << exit_status << ")\n";
            assert(false);
        }
        Internal::debug(2) << "Back from jitted function\n";

        // if the current dst size changed, we have not converted
        bool converged = true;
        for (int i = 0; i < dst.size(); i++) {
            for (int d = 0; d < dst[i].dimensions(); d++) {
                if (dst[i].min(d) != prev_mins[i][d] ||
                    dst[i].extent(d) != prev_extents[i][d])
                {
                    Internal::debug(3) << "infer_input_bounds not yet converged on dst size\n";
                    converged = false;
                }
            }
        }
        if (converged) break;
    }

    // Now allocate the resulting buffers
    size_t j = 0;
    for (size_t i = 0; i < image_param_args.size(); i++) {
        Buffer b = image_param_args[i].second.get_buffer();
        if (!b.defined()) {
            buffer_t buf = dummy_buffers[j];
            Buffer buffer = allocate_dummy_buffer(buf, image_param_args[i].second.type());
            j++;
            image_param_args[i].second.set_buffer(buffer);
        }
    }

    for (size_t i = 0; i < dst.size(); i++) {
        // allocate dst buffer, if needed
        if (!dst[i].host_ptr()) {
            fprintf(stderr, "Allocating dst[%lu]\n", i);
            buffer_t buf = *(dst[i].raw_buffer());
            Buffer buffer = allocate_dummy_buffer(buf, dst[i].type());
            dst[i] = buffer;
        }
        dst[i].set_source_module(compiled_module);
    }
}

void *Func::compile_jit(const Target &target) {
    assert(defined() && "Can't realize undefined function");

    if (!lowered.defined()) lowered = Halide::Internal::lower(func);

    // Infer arguments
    InferArguments infer_args(name());
    lowered.accept(&infer_args);
    arg_values = infer_args.arg_values;

    for (int i = 0; i < func.outputs(); i++) {
        string buffer_name = name();
        if (func.outputs() > 1) {
            buffer_name = buffer_name + '.' + int_to_string(i);
        }
        Type t = func.output_types()[i];
        Argument me(buffer_name, true, t);
        infer_args.arg_types.push_back(me);
        arg_values.push_back(NULL); // A spot to put the address of this output buffer
    }
    image_param_args = infer_args.image_param_args;

    Internal::debug(2) << "Inferred argument list:\n";
    for (size_t i = 0; i < infer_args.arg_types.size(); i++) {
        Internal::debug(2) << infer_args.arg_types[i].name << ", "
                         << infer_args.arg_types[i].type << ", "
                         << infer_args.arg_types[i].is_buffer << "\n";
    }

    Target t = target;
    t.features |= Target::JIT;
    StmtCompiler cg(t);
    cg.compile(lowered, name(), infer_args.arg_types, vector<Buffer>());

    if (debug::debug_level >= 3) {
        cg.compile_to_native(name() + ".s", true);
        cg.compile_to_bitcode(name() + ".bc");
        ofstream stmt_debug((name() + ".stmt").c_str());
        stmt_debug << lowered;
    }

    compiled_module = cg.compile_to_function_pointers();

    return compiled_module.function;
}

void Func::test() {

    Image<int> input(7, 5);
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            input(x, y) = x*y + 10/(y+3);
        }
    }

    Func f, g;
    Var x, y;
    f(x, y) = input(x+1, y) + input(x+1, y)*3 + 1;
    g(x, y) = f(x-1, y) + 2*f(x+1, y);

    f.compute_root();

    Image<int> result = g.realize(5, 5);

    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            int correct = (4*input(x, y)+1) + 2*(4*input(x+2, y)+1);
            if (result(x, y) != correct) {
                std::cerr << "Func test failed: f(" << x << ", " << y << ") = "
                          << result(x, y) << " instead of " << correct << "\n";
                return;
            }
        }
    }

    std::cout << "Func test passed\n";

}

EXPORT Var _("_");
EXPORT Var _0("_0"), _1("_1"), _2("_2"), _3("_3"), _4("_4"),
           _5("_5"), _6("_6"), _7("_7"), _8("_8"), _9("_9");

}
