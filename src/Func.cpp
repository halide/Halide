#include <algorithm>
#include <iostream>
#include <string.h>
#include <fstream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

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
#include "IREquality.h"
#include "HumanReadableStmt.h"
#include "StmtToHtml.h"

namespace Halide {

using std::max;
using std::min;
using std::make_pair;
using std::string;
using std::vector;
using std::pair;
using std::ofstream;

using namespace Internal;

namespace {

Internal::Parameter make_user_context() {
    return Internal::Parameter(type_of<void*>(), false, 0, "__user_context",
        /*is_explicit_name*/ true, /*register_instance*/ false);
}

vector<Argument> add_user_context_arg(vector<Argument> args, const Target& target) {
    for (size_t i = 0; i < args.size(); ++i) {
        internal_assert(!(args[i].type.is_handle() && args[i].name == "__user_context"));
    }
    if (target.has_feature(Target::UserContext)) {
        args.insert(args.begin(), Argument("__user_context", false, Halide::Handle()));
    }
    return args;
}

}  // namespace

Func::Func(const string &name) : func(unique_name(name)),
                                 random_seed(0),
                                 jit_user_context(make_user_context()) {
}

Func::Func() : func(make_entity_name(this, "Halide::Func", 'f')),
               random_seed(0),
               jit_user_context(make_user_context()) {
}

Func::Func(Expr e) : func(make_entity_name(this, "Halide::Func", 'f')),
                     random_seed(0),
                     jit_user_context(make_user_context()) {
    (*this)(_) = e;
}

Func::Func(Function f) : func(f),
                         random_seed(0),
                         jit_user_context(make_user_context()) {
}

Func::~Func() {
    clear_custom_lowering_passes();
}

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
    user_assert(defined())
        << "Can't call Func::value() on an undefined Func. To check if a Func is defined, call Func::defined()\n";
    user_assert(func.outputs() == 1)
        << "Can't call Func::value() on Func \"" << name() << "\", because it has multiple values.\n";
    return func.values()[0];
}

/** The values returned by a Func, in Tuple form. */
Tuple Func::values() const {
    user_assert(defined())
        << "Can't call Func::values() on an undefined Func. To check if a Func is defined, call Func::defined().\n";
    return Tuple(func.values());
}

/** Get the left-hand-side of the update definition. An empty
 * vector if there's no update definition. */
const std::vector<Expr> &Func::update_args(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name()
        << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < (int)func.updates().size())
        << "Update definition index out of bounds.\n";
    return func.updates()[idx].args;
}

/** Get the right-hand-side of the update definition. An error if
 * there is no update definition. */
Expr Func::update_value(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < (int)func.updates().size())
        << "Update definition index out of bounds.\n";
    user_assert(func.updates()[idx].values.size() == 1)
        << "Can't call Func::update_value() on Func \"" << name() << "\", because it has multiple values.\n";
    return func.updates()[idx].values[0];
}

/** The update values returned by a Func, in Tuple form. */
Tuple Func::update_values(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < (int)func.updates().size())
        << "Update definition index out of bounds.\n";
    return Tuple(func.updates()[idx].values);
}

/** Get the reduction domain for the update definition. Returns an
 * undefined RDom if there's no update definition, or if the
 * update definition has no domain. */
RDom Func::reduction_domain(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < (int)func.updates().size())
        << "Update definition index out of bounds.\n";
    return func.updates()[idx].domain;
}

bool Func::defined() const {
    return func.has_pure_definition() || func.has_extern_definition();
}

/** Is this function a reduction? */
bool Func::has_update_definition() const {
    return func.has_update_definition();
}

/** How many update definitions are there? */
int Func::num_update_definitions() const {
    return (int)func.updates().size();
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
    // Bulk up the vars using implicit vars
    vector<Var> args;
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
}

FuncRefVar Func::operator()(Var x) const {
    // Bulk up the vars using implicit vars
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
        user_error << "Func \"" << name() << "\" was called with "
                   << args.size() << " arguments, but was defined with " << dimensions() << "\n";
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
        user_error << "Func \"" << name() << "\" was called with "
                   << args.size() << " arguments, but was defined with " << dimensions() << "\n";
    }

    return placeholder_pos;
}

namespace {
bool var_name_match(string candidate, string var) {
    if (candidate == var) return true;
    return Internal::ends_with(candidate, "." + var);
}
}

const std::string &Stage::name() const {
    return stage_name;
}

void Stage::set_dim_type(VarOrRVar var, For::ForType t) {
    bool found = false;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].for_type = t;

            // If it's an rvar and the for type is parallel, we need to
            // validate that this doesn't introduce a race condition.
            if (!dims[i].pure && var.is_rvar && (t == For::Vectorized || t == For::Parallel)) {
                user_assert(schedule.allow_race_conditions())
                    << "In schedule for " << stage_name
                    << ", marking var " << var.name()
                    << " as parallel or vectorized may introduce a race"
                    << " condition resulting in incorrect output."
                    << " It is possible to override this error using"
                    << " the allow_race_conditions() method. Use this"
                    << " with great caution, and only when you are willing"
                    << " to accept non-deterministic output, or you can prove"
                    << " that any race conditions in this code do not change"
                    << " the output, or you can prove that there are actually"
                    << " no race conditions, and that Halide is being too cautious.\n";
            }

        } else if (t == For::Vectorized) {
            user_assert(dims[i].for_type != For::Vectorized)
                << "In schedule for " << stage_name
                << ", can't vectorize across " << var.name()
                << " because Func is already vectorized across " << dims[i].var << "\n";
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << ", could not find dimension "
                   << var.name()
                   << " to mark as " << t
                   << " in vars for function\n"
                   << dump_argument_list();
    }
}

std::string Stage::dump_argument_list() const {
    std::ostringstream oss;
    oss << "Vars:";
    for (size_t i = 0; i < schedule.dims().size(); i++) {
        oss << " " << schedule.dims()[i].var;
    }
    oss << "\n";
    return oss.str();
}

void Stage::split(const string &old, const string &outer, const string &inner, Expr factor, bool exact) {
    vector<Dim> &dims = schedule.dims();

    // Check that the new names aren't already in the dims list.
    for (size_t i = 0; i < dims.size(); i++) {
        string new_names[2] = {inner, outer};
        for (int j = 0; j < 2; j++) {
            if (var_name_match(dims[i].var, new_names[j]) && new_names[j] != old) {
                user_error << "In schedule for " << stage_name
                           << ", Can't create var " << new_names[j]
                           << " using a split or tile, because " << new_names[j]
                           << " is already used in this Func's schedule elsewhere.\n" << dump_argument_list();
            }
        }
    }

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string inner_name, outer_name, old_name;

    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old)) {
            found = true;
            old_name = dims[i].var;
            inner_name = old_name + "." + inner;
            outer_name = old_name + "." + outer;
            dims.insert(dims.begin() + i, dims[i]);
            dims[i].var = inner_name;
            dims[i+1].var = outer_name;
            dims[i+1].pure = dims[i].pure;
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << "Could not find split dimension: "
                   << old
                   << "\n"
                   << dump_argument_list();
    }

    // Add the split to the splits list
    Split split = {old_name, outer_name, inner_name, factor, exact, Split::SplitVar};
    schedule.splits().push_back(split);
}

Stage &Stage::split(VarOrRVar old, VarOrRVar outer, VarOrRVar inner, Expr factor) {
    if (old.is_rvar) {
        user_assert(outer.is_rvar) << "Can't split RVar " << old.name() << " into Var " << outer.name() << "\n";
        user_assert(inner.is_rvar) << "Can't split RVar " << old.name() << " into Var " << inner.name() << "\n";
    } else {
        user_assert(!outer.is_rvar) << "Can't split Var " << old.name() << " into RVar " << outer.name() << "\n";
        user_assert(!inner.is_rvar) << "Can't split Var " << old.name() << " into RVar " << inner.name() << "\n";
    }
    split(old.name(), outer.name(), inner.name(), factor, old.is_rvar);
    return *this;
}

Stage &Stage::fuse(VarOrRVar inner, VarOrRVar outer, VarOrRVar fused) {
    if (inner.is_rvar) {
        user_assert(outer.is_rvar) << "Can't fuse RVar " << inner.name()
                                   << " with Var " << outer.name() << "\n";
        user_assert(fused.is_rvar) << "Can't fuse RVar " << inner.name()
                                   << "into Var " << fused.name() << "\n";
    } else {
        user_assert(!outer.is_rvar) << "Can't fuse Var " << inner.name()
                                    << " with RVar " << outer.name() << "\n";
        user_assert(!fused.is_rvar) << "Can't fuse Var " << inner.name()
                                    << "into RVar " << fused.name() << "\n";
    }

    // Replace the old dimensions with the new dimension in the dims list
    bool found_outer = false, found_inner = false;
    string inner_name, outer_name, fused_name;
    vector<Dim> &dims = schedule.dims();

    bool outer_pure = false;
    for (size_t i = 0; (!found_outer) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, outer.name())) {
            found_outer = true;
            outer_name = dims[i].var;
            outer_pure = dims[i].pure;
            dims.erase(dims.begin() + i);
        }
    }
    if (!found_outer) {
        user_error << "In schedule for " << stage_name
                   << ", could not find outer fuse dimension: "
                   << outer.name()
                   << "\n"
                   << dump_argument_list();
    }

    for (size_t i = 0; (!found_inner) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, inner.name())) {
            found_inner = true;
            inner_name = dims[i].var;
            fused_name = inner_name + "." + fused.name();
            dims[i].var = fused_name;
            dims[i].pure &= outer_pure;
        }
    }

    if (!found_inner) {
        user_error << "In schedule for " << stage_name
                   << "Could not find inner fuse dimension: "
                   << inner.name()
                   << "\n"
                   << dump_argument_list();
    }

    // Add the fuse to the splits list
    Split split = {fused_name, outer_name, inner_name, Expr(), true, Split::FuseVars};
    schedule.splits().push_back(split);
    return *this;
}

Stage Stage::specialize(Expr condition) {
    user_assert(condition.type().is_bool()) << "Argument passed to specialize must be of type bool\n";

    // The user may be retrieving a reference to an existing
    // specialization.
    for (size_t i = 0; i < schedule.specializations().size(); i++) {
        if (equal(condition, schedule.specializations()[i].condition)) {
            return Stage(schedule.specializations()[i].schedule, stage_name);
        }
    }

    const Specialization &s = schedule.add_specialization(condition);

    return Stage(s.schedule, stage_name);
}

Stage &Stage::rename(VarOrRVar old_var, VarOrRVar new_var) {
    if (old_var.is_rvar) {
        user_assert(new_var.is_rvar)
            << "In schedule for " << stage_name
            << ", can't rename RVar " << old_var.name()
            << " to Var " << new_var.name() << "\n";
    } else {
        user_assert(!new_var.is_rvar)
            << "In schedule for " << stage_name
            << ", can't rename Var " << old_var.name()
            << " to RVar " << new_var.name() << "\n";
    }

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string old_name;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old_var.name())) {
            found = true;
            old_name = dims[i].var;
            dims[i].var += "." + new_var.name();
        }
    }

    string new_name = old_name + "." + new_var.name();

    if (!found) {
        user_error
            << "In schedule for " << stage_name
            << ", could not find rename dimension: "
            << old_var.name()
            << "\n"
            << dump_argument_list();

    }


    // If possible, rewrite the split or rename that defines it.
    found = false;
    for (size_t i = schedule.splits().size(); i > 0; i--) {
        if (schedule.splits()[i-1].is_fuse()) {
            if (schedule.splits()[i-1].inner == old_name ||
                schedule.splits()[i-1].outer == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't rename variable " << old_name
                    << " because it has already been fused into "
                    << schedule.splits()[i-1].old_var << "\n"
                    << dump_argument_list();
            }
            if (schedule.splits()[i-1].old_var == old_name) {
                schedule.splits()[i-1].old_var = new_name;
                found = true;
                break;
            }
        } else {
            if (schedule.splits()[i-1].inner == old_name) {
                schedule.splits()[i-1].inner = new_name;
                found = true;
                break;
            }
            if (schedule.splits()[i-1].outer == old_name) {
                schedule.splits()[i-1].outer = new_name;
                found = true;
                break;
            }
            if (schedule.splits()[i-1].old_var == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't rename a variable " << old_name
                    << " because it has already been renamed or split.\n"
                    << dump_argument_list();
            }
        }
    }

    if (!found) {
        Split split = {old_name, new_name, "", 1, old_var.is_rvar, Split::RenameVar};
        schedule.splits().push_back(split);
    }

    return *this;
}

Stage &Stage::allow_race_conditions() {
    schedule.allow_race_conditions() = true;
    return *this;
}

Stage &Stage::serial(VarOrRVar var) {
    set_dim_type(var, For::Serial);
    return *this;
}

Stage &Stage::parallel(VarOrRVar var) {
    set_dim_type(var, For::Parallel);
    return *this;
}

Stage &Stage::vectorize(VarOrRVar var) {
    set_dim_type(var, For::Vectorized);
    return *this;
}

Stage &Stage::unroll(VarOrRVar var) {
    set_dim_type(var, For::Unrolled);
    return *this;
}

Stage &Stage::parallel(VarOrRVar var, Expr factor) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor);
    }
    parallel(var);
    return *this;
}

Stage &Stage::vectorize(VarOrRVar var, int factor) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor);
        vectorize(tmp);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor);
        vectorize(tmp);
    }
    return *this;
}

Stage &Stage::unroll(VarOrRVar var, int factor) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor);
        unroll(tmp);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor);
        unroll(tmp);
    }

    return *this;
}

Stage &Stage::tile(VarOrRVar x, VarOrRVar y,
                   VarOrRVar xo, VarOrRVar yo,
                   VarOrRVar xi, VarOrRVar yi,
                   Expr xfactor, Expr yfactor) {
    split(x, xo, xi, xfactor);
    split(y, yo, yi, yfactor);
    reorder(xi, yi, xo, yo);
    return *this;
}

Stage &Stage::tile(VarOrRVar x, VarOrRVar y,
                   VarOrRVar xi, VarOrRVar yi,
                   Expr xfactor, Expr yfactor) {
    split(x, x, xi, xfactor);
    split(y, y, yi, yfactor);
    reorder(xi, yi, x, y);
    return *this;
}

namespace {
// An helper function for reordering vars in a schedule.
void reorder_vars(vector<Dim> &dims_old, const VarOrRVar *vars, size_t size, const Stage &stage) {
    vector<Dim> dims = dims_old;

    // Tag all the vars with their locations in the dims list.
    vector<size_t> idx(size);
    for (size_t i = 0; i < size; i++) {
        bool found = false;
        for (size_t j = 0; j < dims.size(); j++) {
            if (var_name_match(dims[j].var, vars[i].name())) {
                idx[i] = j;
                found = true;
            }
        }
        user_assert(found)
            << "In schedule for " << stage.name()
            << ", could not find var " << vars[i].name()
            << " to reorder in the argumemt list.\n"
            << stage.dump_argument_list();
    }

    // Look for illegal reorderings
    for (size_t i = 0; i < idx.size(); i++) {
        if (dims[idx[i]].pure) continue;
        for (size_t j = i+1; j < idx.size(); j++) {
            if (dims[idx[j]].pure) continue;

            if (idx[i] > idx[j]) {
                user_error
                    << "In schedule for " << stage.name()
                    << ", can't reorder RVars " << vars[i].name()
                    << " and " << vars[j].name()
                    << " because it may change the meaning of the algorithm.\n";
            }
        }
    }

    // Sort idx to get the new locations
    vector<size_t> sorted = idx;
    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < size; i++) {
        dims[sorted[i]] = dims_old[idx[i]];
    }

    dims_old.swap(dims);
}
}

Stage &Stage::reorder(const std::vector<VarOrRVar>& vars) {
    reorder_vars(schedule.dims(), &vars[0], vars.size(), *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y) {
    VarOrRVar vars[] = {x, y};
    reorder_vars(schedule.dims(), vars, 2, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z) {
    VarOrRVar vars[] = {x, y, z};
    reorder_vars(schedule.dims(), vars, 3, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w) {
    VarOrRVar vars[] = {x, y, z, w};
    reorder_vars(schedule.dims(), vars, 4, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t) {
    VarOrRVar vars[] = {x, y, z, w, t};
    reorder_vars(schedule.dims(), vars, 5, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2};
    reorder_vars(schedule.dims(), vars, 6, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3};
    reorder_vars(schedule.dims(), vars, 7, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4};
    reorder_vars(schedule.dims(), vars, 8, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4, VarOrRVar t5) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4, t5};
    reorder_vars(schedule.dims(), vars, 9, *this);
    return *this;
}

Stage &Stage::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w, VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4, VarOrRVar t5, VarOrRVar t6) {
    VarOrRVar vars[] = {x, y, z, w, t1, t2, t3, t4, t5, t6};
    reorder_vars(schedule.dims(), vars, 10, *this);
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, GPUAPI /* gpu_api */) {
    parallel(tx);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__thread_id_y", ty.is_rvar));
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__thread_id_y", ty.is_rvar));
    rename(tz, VarOrRVar("__thread_id_z", tz.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, GPUAPI /* gpu_api */) {
    parallel(tx);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, VarOrRVar ty, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__block_id_y", ty.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, GPUAPI /* gpu_api */) {
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__block_id_y", ty.is_rvar));
    rename(tz, VarOrRVar("__block_id_z", tz.is_rvar));
    return *this;
}

Stage &Stage::gpu_single_thread(GPUAPI /* gpu_api */) {
    split(Var::outermost(), Var::outermost(), Var::gpu_blocks(), 1);
    parallel(Var::gpu_blocks());
    return *this;
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar tx, GPUAPI /* gpu_api */) {
    return gpu_blocks(bx).gpu_threads(tx);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by,
                  VarOrRVar tx, VarOrRVar ty, GPUAPI /* gpu_api */) {
    return gpu_blocks(bx, by).gpu_threads(tx, ty);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                  VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                  GPUAPI /* gpu_api */) {
    return gpu_blocks(bx, by, bz).gpu_threads(tx, ty, tz);
}

Stage &Stage::gpu_tile(VarOrRVar x, Expr x_size, GPUAPI /* gpu_api */) {
    VarOrRVar bx("__block_id_x", x.is_rvar),
        tx("__thread_id_x", x.is_rvar);
    split(x, bx, tx, x_size);
    parallel(bx);
    parallel(tx);
    return *this;
}


Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       Expr x_size, Expr y_size,
                       GPUAPI /* gpu_api */) {
    VarOrRVar bx("__block_id_x", x.is_rvar),
        by("__block_id_y", y.is_rvar),
        tx("__thread_id_x", x.is_rvar),
        ty("__thread_id_y", y.is_rvar);
    tile(x, y, bx, by, tx, ty, x_size, y_size);
    parallel(bx);
    parallel(by);
    parallel(tx);
    parallel(ty);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                       Expr x_size, Expr y_size, Expr z_size,
                       GPUAPI /* gpu_api */) {
    VarOrRVar bx("__block_id_x", x.is_rvar),
        by("__block_id_y", y.is_rvar),
        bz("__block_id_z", z.is_rvar),
        tx("__thread_id_x", x.is_rvar),
        ty("__thread_id_y", y.is_rvar),
        tz("__thread_id_z", z.is_rvar);
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

Func &Func::split(VarOrRVar old, VarOrRVar outer, VarOrRVar inner, Expr factor) {
    invalidate_cache();
    Stage(func.schedule(), name()).split(old, outer, inner, factor);
    return *this;
}

Func &Func::fuse(VarOrRVar inner, VarOrRVar outer, VarOrRVar fused) {
    invalidate_cache();
    Stage(func.schedule(), name()).fuse(inner, outer, fused);
    return *this;
}

Func &Func::rename(VarOrRVar old_name, VarOrRVar new_name) {
    invalidate_cache();
    Stage(func.schedule(), name()).rename(old_name, new_name);
    return *this;
}

Func &Func::allow_race_conditions() {
    Stage(func.schedule(), name()).allow_race_conditions();
    return *this;
}

Func &Func::memoize() {
    invalidate_cache();
    func.schedule().memoized() = true;
    return *this;
}

Stage Func::specialize(Expr c) {
    invalidate_cache();
    return Stage(func.schedule(), name()).specialize(c);
}

Func &Func::serial(VarOrRVar var) {
    invalidate_cache();
    Stage(func.schedule(), name()).serial(var);
    return *this;
}

Func &Func::parallel(VarOrRVar var) {
    invalidate_cache();
    Stage(func.schedule(), name()).parallel(var);
    return *this;
}

Func &Func::vectorize(VarOrRVar var) {
    invalidate_cache();
    Stage(func.schedule(), name()).vectorize(var);
    return *this;
}

Func &Func::unroll(VarOrRVar var) {
    invalidate_cache();
    Stage(func.schedule(), name()).unroll(var);
    return *this;
}

Func &Func::parallel(VarOrRVar var, Expr factor) {
    invalidate_cache();
    Stage(func.schedule(), name()).parallel(var, factor);
    return *this;
}

Func &Func::vectorize(VarOrRVar var, int factor) {
    invalidate_cache();
    Stage(func.schedule(), name()).vectorize(var, factor);
    return *this;
}

Func &Func::unroll(VarOrRVar var, int factor) {
    invalidate_cache();
    Stage(func.schedule(), name()).unroll(var, factor);
    return *this;
}

Func &Func::bound(Var var, Expr min, Expr extent) {
    invalidate_cache();
    bool found = false;
    for (size_t i = 0; i < func.args().size(); i++) {
        if (var.name() == func.args()[i]) {
            found = true;
        }
    }
    user_assert(found)
        << "Can't bound variable " << var.name()
        << " of function " << name()
        << " because " << var.name()
        << " is not one of the pure variables of " << name() << ".\n";

    Bound b = {var.name(), min, extent};
    func.schedule().bounds().push_back(b);
    return *this;
}

Func &Func::tile(VarOrRVar x, VarOrRVar y,
                 VarOrRVar xo, VarOrRVar yo,
                 VarOrRVar xi, VarOrRVar yi,
                 Expr xfactor, Expr yfactor) {
    invalidate_cache();
    Stage(func.schedule(), name()).tile(x, y, xo, yo, xi, yi, xfactor, yfactor);
    return *this;
}

Func &Func::tile(VarOrRVar x, VarOrRVar y,
                 VarOrRVar xi, VarOrRVar yi,
                 Expr xfactor, Expr yfactor) {
    invalidate_cache();
    Stage(func.schedule(), name()).tile(x, y, xi, yi, xfactor, yfactor);
    return *this;
}

Func &Func::reorder(const std::vector<VarOrRVar> &vars) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(vars);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t1, t2);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t1, t2, t3);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t1, t2, t3, t4);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4,
                    VarOrRVar t5) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t1, t2, t3, t4, t5);
    return *this;
}

Func &Func::reorder(VarOrRVar x, VarOrRVar y, VarOrRVar z, VarOrRVar w,
                    VarOrRVar t1, VarOrRVar t2, VarOrRVar t3, VarOrRVar t4,
                    VarOrRVar t5, VarOrRVar t6) {
    invalidate_cache();
    Stage(func.schedule(), name()).reorder(x, y, z, w, t1, t2, t3, t4, t5, t6);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, gpu_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, ty, gpu_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, ty, tz, gpu_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, gpu_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, by, gpu_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, by, bz, gpu_api);
    return *this;
}

Func &Func::gpu_single_thread(GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_single_thread(gpu_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar tx, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, tx, gpu_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar tx, VarOrRVar ty, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, by, tx, ty, gpu_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, by, bz, tx, ty, tz, gpu_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, int x_size, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, x_size, gpu_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, int x_size, int y_size, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, y, x_size, y_size, gpu_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z, int x_size, int y_size, int z_size, GPUAPI gpu_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, y, z, x_size, y_size, z_size, gpu_api);
    return *this;
}

Func &Func::glsl(Var x, Var y, Var c) {
    invalidate_cache();

    reorder(c, x, y);
    // GLSL outputs must be stored interleaved
    reorder_storage(c, x, y);

    // TODO: Set appropriate constraints if this is the output buffer?

    Stage(func.schedule(), name()).gpu_blocks(x, y);

    bool constant_bounds = false;
    Schedule &sched = func.schedule();
    for (size_t i = 0; i < sched.bounds().size(); i++) {
        if (c.name() == sched.bounds()[i].var) {
            constant_bounds = is_const(sched.bounds()[i].min) &&
                is_const(sched.bounds()[i].extent);
            break;
        }
    }
    user_assert(constant_bounds)
        << "The color channel for GLSL loops must have constant bounds, e.g., .bound(c, 0, 3).\n";
    vectorize(c);
    return *this;
}

Func &Func::reorder_storage(Var x, Var y) {
    invalidate_cache();

    vector<string> &dims = func.schedule().storage_dims();
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
    user_error << "Could not find variables " << x.name()
               << " and " << y.name() << " to reorder in schedule.\n";
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
    invalidate_cache();
    LoopLevel loop_level(f.name(), var.name());
    func.schedule().compute_level() = loop_level;
    if (func.schedule().store_level().is_inline()) {
        func.schedule().store_level() = loop_level;
    }
    return *this;
}

Func &Func::compute_root() {
    invalidate_cache();
    func.schedule().compute_level() = LoopLevel::root();
    if (func.schedule().store_level().is_inline()) {
        func.schedule().store_level() = LoopLevel::root();
    }
    return *this;
}

Func &Func::store_at(Func f, RVar var) {
    return store_at(f, Var(var.name()));
}

Func &Func::store_at(Func f, Var var) {
    invalidate_cache();
    func.schedule().store_level() = LoopLevel(f.name(), var.name());
    return *this;
}

Func &Func::store_root() {
    invalidate_cache();
    func.schedule().store_level() = LoopLevel::root();
    return *this;
}

Func &Func::compute_inline() {
    invalidate_cache();
    func.schedule().compute_level() = LoopLevel();
    func.schedule().store_level() = LoopLevel();
    return *this;
}

Func &Func::trace_loads() {
    invalidate_cache();
    func.trace_loads();
    return *this;
}

Func &Func::trace_stores() {
    invalidate_cache();
    func.trace_stores();
    return *this;
}

Func &Func::trace_realizations() {
    invalidate_cache();
    func.trace_realizations();
    return *this;
}

void Func::debug_to_file(const string &filename) {
    invalidate_cache();
    func.debug_file() = filename;
}

Stage Func::update(int idx) {
    user_assert(idx < num_update_definitions()) <<
      "Call to update with index larger than last defined update stage for Func \"" <<
      name() << "\".\n";
    invalidate_cache();
    return Stage(func.update_schedule(idx),
                 name() + ".update(" + int_to_string(idx) + ")");
}

void Func::invalidate_cache() {
    lowered = Stmt();
    compiled_module = JITModule();
}

Func::operator Stage() const {
    return Stage(func.schedule(), name());
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

    for (size_t i = 0; i < e.size(); i++) {
        user_assert(e[i].defined())
            << "Argument " << i << " in call to \"" << func.name() << "\" is undefined.\n";
    }

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
        user_assert(found)
            << "Right-hand-side of pure definition of " << func.name()
            << " uses implicit variables, but the left-hand-side does not"
            << " contain the placeholder symbol '_'.\n";
    }

    return a;
}

Stage FuncRefVar::operator=(Expr e) {
    return (*this) = Tuple(vec<Expr>(e));
}

Stage FuncRefVar::operator=(const Tuple &e) {
    // If the function has already been defined, this must actually be an update
    if (func.has_pure_definition()) {
        return FuncRefExpr(func, args) = e;
    }

    // Find implicit args in the expr and add them to the args list before calling define
    vector<string> a = args_with_implicit_vars(e.as_vector());
    func.define(a, e.as_vector());

    return Stage(func.schedule(), func.name());
}

Stage FuncRefVar::operator=(const FuncRefVar &e) {
    if (e.size() == 1) {
        return (*this) = Expr(e);
    } else {
        return (*this) = Tuple(e);
    }
}

Stage FuncRefVar::operator=(const FuncRefExpr &e) {
    if (e.size() == 1) {
        return (*this) = Expr(e);
    } else {
        return (*this) = Tuple(e);
    }
}

Stage FuncRefVar::operator+=(Expr e) {
    // This is actually an update
    return FuncRefExpr(func, args) += e;
}

Stage FuncRefVar::operator*=(Expr e) {
    // This is actually an update
    return FuncRefExpr(func, args) *= e;
}

Stage FuncRefVar::operator-=(Expr e) {
    // This is actually an update
    return FuncRefExpr(func, args) -= e;
}

Stage FuncRefVar::operator/=(Expr e) {
    // This is actually an update
    return FuncRefExpr(func, args) /= e;
}

FuncRefVar::operator Expr() const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";
    vector<Expr> expr_args(args.size());
    for (size_t i = 0; i < expr_args.size(); i++) {
        expr_args[i] = Var(args[i]);
    }
    user_assert(func.outputs() == 1)
        << "Can't convert a reference Func \"" << func.name()
        << "\" to an Expr, because \"" << func.name() << "\" returns a Tuple.\n";
    return Call::make(func, expr_args);
}

Expr FuncRefVar::operator[](int i) const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";

    user_assert(func.outputs() != 1)
        << "Can't index into a reference to Func \"" << func.name()
        << "\", because it does not return a Tuple.\n";
    user_assert(i >= 0 && i < func.outputs())
        << "Tuple index out of range in reference to Func \"" << func.name() << "\".\n";
    vector<Expr> expr_args(args.size());
    for (size_t j = 0; j < expr_args.size(); j++) {
        expr_args[j] = Var(args[j]);
    }
    return Call::make(func, expr_args, i);
}

size_t FuncRefVar::size() const {
    return func.outputs();
}

FuncRefExpr::FuncRefExpr(Internal::Function f, const vector<Expr> &a, int placeholder_pos) : func(f), args(a) {
    implicit_placeholder_pos = placeholder_pos;
    Internal::check_call_arg_types(f.name(), &args, args.size());
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

    for (size_t i = 0; i < e.size(); i++) {
        user_assert(e[i].defined())
            << "Argument " << (i+1) << " in call to \"" << func.name() << "\" is undefined.\n";
    }

    CountImplicitVars count(e);
    // TODO: Check if there is a test case for this and add one if not.
    // Implicit vars are also allowed in the lhs of an update. E.g.:
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
        user_assert(found)
            << "Right-hand-side of update definition of " << func.name()
            << " uses implicit variables, but the left-hand-side does not"
            << " contain the placeholder symbol '_'.\n";
    }

    return a;
}

Stage FuncRefExpr::operator=(Expr e) {
    return (*this) = Tuple(vec<Expr>(e));
}

Stage FuncRefExpr::operator=(const Tuple &e) {
    user_assert(func.has_pure_definition())
        << "Can't add an update definition to Func \"" << func.name()
        << "\" because it does not have a pure definition.\n";

    vector<Expr> a = args_with_implicit_vars(e.as_vector());
    func.define_update(args, e.as_vector());

    int update_stage = func.updates().size() - 1;
    return Stage(func.update_schedule(update_stage),
                 func.name() + ".update(" + int_to_string(update_stage) + ")");
}

Stage FuncRefExpr::operator=(const FuncRefExpr &e) {
    if (e.size() == 1) {
        return (*this) = Expr(e);
    } else {
        return (*this) = Tuple(e);
    }
}

Stage FuncRefExpr::operator=(const FuncRefVar &e) {
    if (e.size() == 1) {
        return (*this) = Expr(e);
    } else {
        return (*this) = Tuple(e);
    }
}

// Inject a suitable base-case definition given an update
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

Stage FuncRefExpr::operator+=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 0));
    return (*this) = Expr(*this) + e;
}

Stage FuncRefExpr::operator*=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 1));
    return (*this) = Expr(*this) * e;
}

Stage FuncRefExpr::operator-=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 0));
    return (*this) = Expr(*this) - e;
}

Stage FuncRefExpr::operator/=(Expr e) {
    vector<Expr> a = args_with_implicit_vars(vec(e));
    define_base_case(func, a, cast(e.type(), 1));
    return (*this) = Expr(*this) / e;
}

FuncRefExpr::operator Expr() const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";

    user_assert(func.outputs() == 1)
        << "Can't convert a reference Func \"" << func.name()
        << "\" to an Expr, because " << func.name() << " returns a Tuple.\n";

    return Call::make(func, args);
}

Expr FuncRefExpr::operator[](int i) const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";

    user_assert(func.outputs() != 1)
        << "Can't index into a reference to Func \"" << func.name()
        << "\", because it does not return a Tuple.\n";

    user_assert(i >= 0 && i < func.outputs())
        << "Tuple index out of range in reference to Func \"" << func.name() << "\".\n";

    return Call::make(func, args, i);
}

size_t FuncRefExpr::size() const {
    return func.outputs();
}

Realization Func::realize(std::vector<int32_t> sizes, const Target &target) {
    user_assert(defined()) << "Can't realize undefined Func.\n";
    vector<Buffer> outputs(func.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(func.output_types()[i], sizes);
    }
    Realization r(outputs);
    realize(r, target);
    return r;
}

Realization Func::realize(int x_size, int y_size, int z_size, int w_size, const Target &target) {
    user_assert(defined()) << "Can't realize undefined Func.\n";
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
    user_assert(defined()) << "Can't infer input bounds on an undefined Func.\n";
    vector<Buffer> outputs(func.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(func.output_types()[i], x_size, y_size, z_size, w_size, (uint8_t *)1);
    }
    Realization r(outputs);
    infer_input_bounds(r);
}

OutputImageParam Func::output_buffer() const {
    user_assert(defined())
        << "Can't access output buffer of undefined Func.\n";
    user_assert(func.output_buffers().size() == 1)
        << "Can't call Func::output_buffer on Func \"" << name()
        << "\" because it returns a Tuple.\n";
    return OutputImageParam(func.output_buffers()[0]);
}

vector<OutputImageParam> Func::output_buffers() const {
    user_assert(defined())
        << "Can't access output buffers of undefined Func.\n";

    vector<OutputImageParam> bufs(func.output_buffers().size());
    for (size_t i = 0; i < bufs.size(); i++) {
        bufs[i] = OutputImageParam(func.output_buffers()[i]);
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

    InferArguments(const string &o, bool include_buffers = true)
        : output(o), include_buffers(include_buffers) {
    }

    void visit_function(const Function& func) {
        if (func.has_pure_definition()) {
            visit_exprs(func.values());
        }
        for (std::vector<UpdateDefinition>::const_iterator update = func.updates().begin();
             update != func.updates().end();
             ++update) {
            visit_exprs(update->values);
            visit_exprs(update->args);
            if (update->domain.defined()) {
                for (std::vector<ReductionVariable>::const_iterator rvar = update->domain.domain().begin();
                     rvar != update->domain.domain().end();
                     ++rvar) {
                visit_expr(rvar->min);
                visit_expr(rvar->extent);
            }
          }
        }
        if (func.has_extern_definition()) {
            for (std::vector<ExternFuncArgument>::const_iterator extern_arg = func.extern_arguments().begin();
                 extern_arg != func.extern_arguments().end();
                 ++extern_arg) {
            if (extern_arg->is_func()) {
                visit_function(extern_arg->func);
            } else if (extern_arg->is_expr()) {
                visit_expr(extern_arg->expr);
            } else if (extern_arg->is_buffer()) {
                include_parameter(Parameter(extern_arg->buffer.type(), true,
                                            extern_arg->buffer.dimensions(),
                                            extern_arg->buffer.name()));
            } else if (extern_arg->is_image_param()) {
                include_parameter(extern_arg->image_param);
            }
          }
        }
        for (std::vector<Parameter>::const_iterator buf = func.output_buffers().begin();
             buf != func.output_buffers().end();
             ++buf) {
            for (int i = 0; i < std::min(func.dimensions(), 4); i++) {
                visit_expr(buf->min_constraint(i));
                visit_expr(buf->stride_constraint(i));
                visit_expr(buf->extent_constraint(i));
            }
        }
    }

private:
    const string &output;
    const bool include_buffers;

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

    void visit_exprs(const std::vector<Expr>& v) {
        for (std::vector<Expr>::const_iterator it = v.begin(); it != v.end(); ++it) {
            visit_expr(*it);
        }
    }

    void visit_expr(Expr e) {
        if (!e.defined()) return;
        e.accept(this);
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
        if (!include_buffers) return;
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
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        visit_function(op->func);
        include_buffer(op->image);
        include_parameter(op->param);
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
                internal_assert(buf.defined());
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
            std::ostringstream err;
            err << "Generated code refers to ";
            if (arg.is_buffer) err << "image ";
            err << "parameter " << arg.name
                << ", which was not found in the argument list.\n";

            err << "\nArgument list specified: ";
            for (size_t i = 0; i < args.size(); i++) {
                err << args[i].name << " ";
            }
            err << "\n\nParameters referenced in generated code: ";
            for (size_t i = 0; i < required_args.size(); i++) {
                err << required_args[i].name << " ";
            }
            err << "\n\n";
            user_error << err.str();
        }
    }
}

// Sort the Arguments with all buffers first (alphabetical by name),
// followed by all non-buffers (alphabetical by name).
struct ArgumentComparator {
    bool operator()(const Argument& a, const Argument& b) {
        if (a.is_buffer != b.is_buffer)
            return a.is_buffer;
        else
            return a.name < b.name;
    }
};
}

std::vector<Argument> Func::infer_arguments() const {
    user_assert(defined()) << "Can't infer arguments for undefined Func.\n";

    InferArguments infer_args(name(), /*include_buffers*/ false);
    infer_args.visit_function(func);

    std::sort(infer_args.arg_types.begin(), infer_args.arg_types.end(), ArgumentComparator());

    return infer_args.arg_types;
}

void Func::lower(const Target &t) {
    if (!lowered.defined() || t != lowered_target) {
        vector<IRMutator *> custom_passes;
        for (size_t i = 0; i < custom_lowering_passes.size(); i++) {
            custom_passes.push_back(custom_lowering_passes[i].pass);
        }
        lowered = Halide::Internal::lower(func, t, custom_passes);
        lowered_target = t;

        // Forbid new definitions of the func
        func.freeze();
    }
}

void Func::compile_to(const Outputs &output_files, vector<Argument> args,
                      const string &fn_name, const Target &target) {
    user_assert(defined()) << "Can't compile undefined Func.\n";

    args = add_user_context_arg(args, target);

    lower(target);

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    StmtCompiler cg(target);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);

    if (!output_files.object_name.empty()) {
        cg.compile_to_native(output_files.object_name, false);
    }
    if (!output_files.assembly_name.empty()) {
        cg.compile_to_native(output_files.assembly_name, true);
    }
    if (!output_files.bitcode_name.empty()) {
        cg.compile_to_bitcode(output_files.bitcode_name);
    }
}

void Func::compile_to_bitcode(const string &filename, vector<Argument> args, const string &fn_name,
                              const Target &target) {
    compile_to(Outputs().bitcode(filename), args, fn_name, target);
}

void Func::compile_to_bitcode(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_bitcode(filename, args, "", target);
}

void Func::compile_to_object(const string &filename, vector<Argument> args,
                             const string &fn_name, const Target &target) {
    compile_to(Outputs().object(filename), args, fn_name, target);
}

void Func::compile_to_object(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_object(filename, args, "", target);
}

void Func::compile_to_header(const string &filename, vector<Argument> args, const string &fn_name, const Target &target) {
    args = add_user_context_arg(args, target);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    ofstream header(filename.c_str());
    CodeGen_C cg(header);
    cg.compile_header(fn_name.empty() ? name() : fn_name, args);
}

void Func::compile_to_c(const string &filename, vector<Argument> args,
                        const string &fn_name, const Target &target) {
    args = add_user_context_arg(args, target);

    lower(target);

    vector<Buffer> images_to_embed;
    validate_arguments(name(), args, lowered, images_to_embed);

    for (int i = 0; i < outputs(); i++) {
        args.push_back(output_buffers()[i]);
    }

    ofstream src(filename.c_str());
    CodeGen_C cg(src);
    cg.compile(lowered, fn_name.empty() ? name() : fn_name, args, images_to_embed);
}

void Func::compile_to_lowered_stmt(const string &filename, StmtOutputFormat fmt, const Target &target) {
    lower(target);
    if (fmt == HTML) {
        print_to_html(filename, lowered);
    } else {
        ofstream stmt_output(filename.c_str());
        stmt_output << lowered;
    }
}


void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              Realization dst,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    return compile_to_simplified_lowered_stmt(filename, dst[0], std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              Realization dst,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    return compile_to_simplified_lowered_stmt(filename, dst[0], std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              Buffer dst,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    return compile_to_simplified_lowered_stmt(filename, dst, std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              Buffer dst,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {

    lower(t);

    Stmt s = human_readable_stmt(function(), lowered, dst, additional_replacements);

    if (fmt == HTML) {
        print_to_html(filename, s);
    } else {
        ofstream stmt_output(filename.c_str());
        stmt_output << s;
    }
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size, int z_size, int w_size,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    // Make a dummy host pointer to avoid a pointless allocation.
    uint8_t dummy_data = 0;
    Buffer output_buf(output_types()[0], x_size, y_size, z_size, w_size, &dummy_data);

    compile_to_simplified_lowered_stmt(filename, output_buf, additional_replacements, fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size, int z_size, int w_size,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, y_size, z_size, w_size,
                                       std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size, int z_size,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, y_size, z_size, 0, additional_replacements, fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size, int z_size,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, y_size, z_size, 0,
                                       std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, y_size, 0, 0,
                                       additional_replacements, fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size, int y_size,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, y_size, 0, 0,
                                       std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size,
                                              const std::map<std::string, Expr> &additional_replacements,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, 0, 0, 0,
                                       additional_replacements, fmt, t);
}

void Func::compile_to_simplified_lowered_stmt(const std::string &filename,
                                              int x_size,
                                              StmtOutputFormat fmt,
                                              const Target &t) {
    compile_to_simplified_lowered_stmt(filename, x_size, 0, 0, 0,
                                       std::map<std::string, Expr>(), fmt, t);
}

void Func::compile_to_file(const string &filename_prefix, vector<Argument> args,
                           const Target &target) {
    compile_to_header(filename_prefix + ".h", args, filename_prefix, target);
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
    compile_to(Outputs().assembly(filename), args, fn_name, target);
}

void Func::compile_to_assembly(const string &filename, vector<Argument> args, const Target &target) {
    compile_to_assembly(filename, args, "", target);
}

void Func::set_error_handler(void (*handler)(void *, const char *)) {
    jit_handlers.custom_error = handler;
}

void Func::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                void (*cust_free)(void *, void *)) {
    jit_handlers.custom_malloc = cust_malloc;
    jit_handlers.custom_free = cust_free;
}

void Func::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    jit_handlers.custom_do_par_for = cust_do_par_for;
}

void Func::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    jit_handlers.custom_do_task = cust_do_task;
}

void Func::set_custom_trace(int (*trace_fn)(void *, const halide_trace_event *)) {
    jit_handlers.custom_trace = trace_fn;
}

void Func::set_custom_print(void (*cust_print)(void *, const char *)) {
    jit_handlers.custom_print = cust_print;
}

void Func::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
    invalidate_cache();
    CustomLoweringPass p = {pass, deleter};
    custom_lowering_passes.push_back(p);
}

void Func::clear_custom_lowering_passes() {
    invalidate_cache();
    for (size_t i = 0; i < custom_lowering_passes.size(); i++) {
        if (custom_lowering_passes[i].deleter) {
            custom_lowering_passes[i].deleter(custom_lowering_passes[i].pass);
        }
    }
    custom_lowering_passes.clear();
}

void Func::realize(Buffer b, const Target &target) {
    realize(Realization(vec<Buffer>(b)), target);
}

namespace Internal {

struct ErrorBuffer {
    enum { MaxBufSize = 4096 };
    char buf[MaxBufSize];
    int end;

    ErrorBuffer() {
        end = 0;
    }

    void concat(const char *message) {
        size_t len = strlen(message);

        if (len && message[len-1] != '\n') {
            // Claim some extra space for a newline.
            len++;
        }

        // Atomically claim some space in the buffer
#ifdef _MSC_VER
        int old_end = _InterlockedExchangeAdd((volatile long *)(&end), len);
#else
        int old_end = __sync_fetch_and_add(&end, len);
#endif

        if (old_end + len >= MaxBufSize - 1) {
            // Out of space
            return;
        }

        for (size_t i = 0; i < len - 1; i++) {
            buf[old_end + i] = message[i];
        }
        if (buf[old_end + len - 2] != '\n') {
            buf[old_end + len - 1] = '\n';
        }
    }

    std::string str() const {
        return std::string(buf, end);
    }

    static void handler(void *ctx, const char *message) {
        if (ctx) {
            JITUserContext *ctx1 = (JITUserContext *)ctx;
            ErrorBuffer *buf = (ErrorBuffer *)ctx1->user_context;
            buf->concat(message);
        }
    }
};

struct JITFuncCallContext {
    ErrorBuffer error_buffer;
    JITUserContext jit_context;
    Internal::Parameter &user_context_param;

    JITFuncCallContext(const JITHandlers &handlers, Internal::Parameter &user_context_param)
        : user_context_param(user_context_param) {
        void *user_context = NULL;
        JITHandlers local_handlers = handlers;
        if (local_handlers.custom_error == NULL) {
            local_handlers.custom_error = Internal::ErrorBuffer::handler;
            user_context = &error_buffer;
        }
        JITSharedRuntime::init_jit_user_context(jit_context, user_context, local_handlers);
        user_context_param.set_scalar(&jit_context);
    }

    void report_if_error(int exit_status) {
        if (exit_status) {
            std::string output = error_buffer.str();
            if (!output.empty()) {
                // Only report the errors if no custom error handler was installed
                halide_runtime_error << error_buffer.str();
                error_buffer.end = 0;
            }
        }
    }

    void finalize(int exit_status) {
        report_if_error(exit_status);
        user_context_param.set_scalar((void *)NULL); // Don't leave param hanging with pointer to stack.
    }
};

}  // namespace Internal

void Func::realize(Realization dst, const Target &target) {
    if (!compiled_module.jit_wrapper_function()) {
        compile_jit(target);
    }

    internal_assert(compiled_module.jit_wrapper_function());

    // Check the type and dimensionality of the buffer
    for (size_t i = 0; i < dst.size(); i++) {
        user_assert(dst[i].dimensions() == dimensions())
            << "Can't realize Func \"" << name()
            << "\" into Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" is " << dst[i].dimensions() << "-dimensional"
            << ", but Func \"" << name()
            << "\" is " << dimensions() << "-dimensional.\n";
        user_assert(dst[i].type() == func.output_types()[i])
            << "Can't realize Func \"" << name()
            << "\" into Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" has type " << dst[i].type()
            << ", but Func \"" << name()
            << "\" has type " << func.output_types()[i] << ".\n";
    }

    JITFuncCallContext jit_context(jit_handlers, jit_user_context);

    // Update the address of the buffers we're realizing into
    for (size_t i = 0; i < dst.size(); i++) {
        arg_values[arg_values.size()-dst.size()+i] = dst[i].raw_buffer();
    }

    // Update the addresses of the image param args
    Internal::debug(3) << image_param_args.size() << " image param args to set\n";
    for (size_t i = 0; i < image_param_args.size(); i++) {
        Internal::debug(3) << "Updating address for image param: " << image_param_args[i].second.name() << "\n";
        Buffer b = image_param_args[i].second.get_buffer();
        user_assert(b.defined())
            << "ImageParam \"" << image_param_args[i].second.name()
            << "\" is not bound to a buffer.\n";
        buffer_t *buf = b.raw_buffer();
        arg_values[image_param_args[i].first] = buf;
        user_assert(buf->host || buf->dev)
            << "ImageParam \"" << image_param_args[i].second.name()
            << "\" is bound to Buffer " << b.name()
            << " which has NULL host and dev pointers\n";
    }

    for (size_t i = 0; i < arg_values.size(); i++) {
        Internal::debug(2) << "Arg " << i << " = " << arg_values[i] << " (" << *(void * const *)arg_values[i] << ")\n";
        internal_assert(arg_values[i])
            << "An argument to a jitted function is null\n";
    }

    // Always add a custom error handler to capture any error messages.
    // (If there is a user-set error handler, it will be called as well.)

    Internal::debug(2) << "Calling jitted function\n";
    int exit_status = compiled_module.jit_wrapper_function()(&(arg_values[0]));
    Internal::debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    // TODO: Remove after Buffer is sorted out.
    for (size_t i = 0; i < dst.size(); i++) {
        dst[i].set_source_module(compiled_module);
    }

    jit_context.finalize(exit_status);
}

void Func::infer_input_bounds(Buffer dst) {
    infer_input_bounds(Realization(vec<Buffer>(dst)));
}

void Func::infer_input_bounds(Realization dst) {
    if (!compiled_module.jit_wrapper_function()) {
        compile_jit();
    }

    internal_assert(compiled_module.jit_wrapper_function());

    JITFuncCallContext jit_context(jit_handlers, jit_user_context);

    // Check the type and dimensionality of the buffer
    for (size_t i = 0; i < dst.size(); i++) {
        user_assert(dst[i].dimensions() == dimensions())
            << "Can't infer input bounds for Func \"" << name()
            << "\" using output Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" is " << dst[i].dimensions() << "-dimensional"
            << ", but Func \"" << name()
            << "\" is " << dimensions() << "-dimensional.\n";
        user_assert(dst[i].type() == func.output_types()[i])
            << "Can't infer input bounds for Func \"" << name()
            << "\" using output Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" has type " << dst[i].type()
            << ", but Func \"" << name()
            << "\" has type " << func.output_types()[i] << ".\n";
    }

    // Update the address of the buffers we're realizing into
    for (size_t i = 0; i < dst.size(); i++) {
        arg_values[arg_values.size()-dst.size()+i] = dst[i].raw_buffer();
    }

    // Update the addresses of the image param args
    Internal::debug(3) << image_param_args.size() << " image param args to set\n";
    vector<buffer_t> dummy_buffers;
    // We're going to be taking addresses of elements as we push_back,
    // so reserve enough space to avoid reallocation.
    dummy_buffers.reserve(image_param_args.size());
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
        Internal::debug(2) << "Arg " << i << " = " << arg_values[i] << " (" << *(void * const *)arg_values[i] << ")\n";
        internal_assert(arg_values[i]) << "An argument to a jitted function is null.\n";
    }

    // Figure out which buffers to watch for changes
    vector<const buffer_t *> tracked_buffers;
    for (size_t i = 0; i < dummy_buffers.size(); i++) {
        tracked_buffers.push_back(&dummy_buffers[i]);
    }
    for (size_t i = 0; i < dst.size(); i++) {
        if (dst[i].host_ptr() == NULL) {
            tracked_buffers.push_back(dst[i].raw_buffer());
        }
    }
    vector<buffer_t> old_buffer(tracked_buffers.size());

    const int max_iters = 16;
    int iter = 0;

    for (iter = 0; iter < max_iters; iter++) {
        // Make a copy of the buffers we expect to be mutated
        for (size_t j = 0; j < tracked_buffers.size(); j++) {
            old_buffer[j] = *tracked_buffers[j];
        }
        Internal::debug(2) << "Calling jitted function\n";
        int exit_status = compiled_module.jit_wrapper_function()(&(arg_values[0]));

        jit_context.report_if_error(exit_status);

        Internal::debug(2) << "Back from jitted function\n";
        bool changed = false;

        // Check if there were any changed
        for (size_t j = 0; j < tracked_buffers.size(); j++) {
            if (memcmp(&old_buffer[j], tracked_buffers[j], sizeof(buffer_t))) {
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    jit_context.finalize(0);

    user_assert(iter < max_iters)
        << "Inferring input bounds on Func \"" << name() << "\""
        << " didn't converge after " << max_iters
        << " iterations. There may be unsatisfiable constraints\n";

    // Now allocate the resulting buffers
    size_t j = 0;
    for (size_t i = 0; i < image_param_args.size(); i++) {
        Buffer b = image_param_args[i].second.get_buffer();
        if (!b.defined()) {
            buffer_t buf = dummy_buffers[j];

            Internal::debug(1) << "Inferred bounds for " << image_param_args[i].second.name() << ": ("
                               << buf.min[0] << ","
                               << buf.min[1] << ","
                               << buf.min[2] << ","
                               << buf.min[3] << ")..("
                               << buf.min[0] + buf.extent[0] << ","
                               << buf.min[1] + buf.extent[1] << ","
                               << buf.min[2] + buf.extent[2] << ","
                               << buf.min[3] + buf.extent[3] << ")\n";

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
            Buffer buffer(image_param_args[i].second.type(), total_size,
                          buf.extent[1] > 0 ? 1 : 0,
                          buf.extent[2] > 0 ? 1 : 0,
                          buf.extent[3] > 0 ? 1 : 0);

            // Rewrite the buffer fields to match the ones returned
            for (int d = 0; d < 4; d++) {
                buffer.raw_buffer()->min[d] = buf.min[d];
                buffer.raw_buffer()->stride[d] = buf.stride[d];
                buffer.raw_buffer()->extent[d] = buf.extent[d];
            }
            j++;
            image_param_args[i].second.set_buffer(buffer);
        }
    }

    // TODO: Remove after Buffer is sorted out.
    for (size_t i = 0; i < dst.size(); i++) {
        dst[i].set_source_module(compiled_module);
    }
}

void *Func::compile_jit(const Target &target_arg) {
    user_assert(defined()) << "Can't jit-compile undefined Func.\n";

    Target target(target_arg);
    target.set_feature(Target::JIT);
    target.set_feature(Target::UserContext);

    lower(target);

    // Infer arguments
    InferArguments infer_args(name());
    lowered.accept(&infer_args);

    // For jitting, we always add jit_user_context,
    // regardless of whether Target::UserContext is set.
    Expr uc_expr = Internal::Variable::make(type_of<void*>(), jit_user_context.name(), jit_user_context);
    uc_expr.accept(&infer_args);

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

    StmtCompiler cg(target);

    // Sanitise the name of the generated function
    string n = name();
    for (size_t i = 0; i < n.size(); i++) {
        if (!isalnum(n[i])) {
            n[i] = '_';
        }
    }

    cg.compile(lowered, n, infer_args.arg_types, vector<Buffer>());

    if (debug::debug_level >= 3) {
        cg.compile_to_native(name() + ".s", true);
        cg.compile_to_bitcode(name() + ".bc");
        ofstream stmt_debug((name() + ".stmt").c_str());
        stmt_debug << lowered;
    }

    compiled_module = cg.compile_to_function_pointers();

    return compiled_module.main_function();
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
