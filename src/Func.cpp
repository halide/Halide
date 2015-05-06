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
#include "Image.h"
#include "Param.h"
#include "PrintLoopNest.h"
#include "Debug.h"
#include "IREquality.h"
#include "CodeGen_LLVM.h"
#include "LLVM_Headers.h"
#include "Output.h"
#include "LLVM_Output.h"

namespace Halide {

using std::max;
using std::min;
using std::make_pair;
using std::string;
using std::vector;
using std::pair;
using std::ofstream;

using namespace Internal;

Func::Func(const string &name) : func(unique_name(name)) {}

Func::Func() : func(make_entity_name(this, "Halide::Func", 'f')) {}

Func::Func(Expr e) : func(make_entity_name(this, "Halide::Func", 'f')) {
    (*this)(_) = e;
}

Func::Func(Function f) : func(f) {}

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
    user_assert(idx < num_update_definitions())
        << "Update definition index out of bounds.\n";
    return func.updates()[idx].args;
}

/** Get the right-hand-side of the update definition. An error if
 * there is no update definition. */
Expr Func::update_value(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < num_update_definitions())
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
    user_assert(idx < num_update_definitions())
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
    user_assert(idx < num_update_definitions())
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
    return static_cast<int>(func.updates().size());
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

FuncRefVar Func::operator()(vector<Var> args) const {
    int placeholder_pos = add_implicit_vars(args);
    return FuncRefVar(func, args, placeholder_pos);
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
        if (var && var->name == _.name())
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

void Stage::set_dim_type(VarOrRVar var, ForType t) {
    bool found = false;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].for_type = t;

            // If it's an rvar and the for type is parallel, we need to
            // validate that this doesn't introduce a race condition.
            if (!dims[i].pure && var.is_rvar && (t == ForType::Vectorized || t == ForType::Parallel)) {
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

        } else if (t == ForType::Vectorized) {
            user_assert(dims[i].for_type != ForType::Vectorized)
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

void Stage::set_dim_device_api(VarOrRVar var, DeviceAPI device_api) {
    bool found = false;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].device_api = device_api;
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << ", could not find dimension "
                   << var.name()
                   << " to set to device API " << static_cast<int>(device_api)
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
    set_dim_type(var, ForType::Serial);
    return *this;
}

Stage &Stage::parallel(VarOrRVar var) {
    set_dim_type(var, ForType::Parallel);
    return *this;
}

Stage &Stage::vectorize(VarOrRVar var) {
    set_dim_type(var, ForType::Vectorized);
    return *this;
}

Stage &Stage::unroll(VarOrRVar var) {
    set_dim_type(var, ForType::Unrolled);
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
            << " to reorder in the argument list.\n"
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

Stage &Stage::gpu_threads(VarOrRVar tx, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    parallel(tx);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    parallel(tx);
    parallel(ty);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__thread_id_y", ty.is_rvar));
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_device_api(tz, device_api);
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, VarOrRVar("__thread_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__thread_id_y", ty.is_rvar));
    rename(tz, VarOrRVar("__thread_id_z", tz.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    parallel(tx);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    parallel(tx);
    parallel(ty);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__block_id_y", ty.is_rvar));
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_device_api(tz, device_api);
    parallel(tx);
    parallel(ty);
    parallel(tz);
    rename(tx, VarOrRVar("__block_id_x", tx.is_rvar));
    rename(ty, VarOrRVar("__block_id_y", ty.is_rvar));
    rename(tz, VarOrRVar("__block_id_z", tz.is_rvar));
    return *this;
}

Stage &Stage::gpu_single_thread(DeviceAPI device_api) {
    split(Var::outermost(), Var::outermost(), Var::gpu_blocks(), 1);
    set_dim_device_api(Var::gpu_blocks(), device_api);
    parallel(Var::gpu_blocks());
    return *this;
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar tx, DeviceAPI device_api) {
    return gpu_blocks(bx).gpu_threads(tx);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by,
                  VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    return gpu_blocks(bx, by).gpu_threads(tx, ty);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                  VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                  DeviceAPI device_api) {
    return gpu_blocks(bx, by, bz).gpu_threads(tx, ty, tz);
}

Stage &Stage::gpu_tile(VarOrRVar x, Expr x_size, DeviceAPI device_api) {
    VarOrRVar bx("__block_id_x", x.is_rvar),
        tx("__thread_id_x", x.is_rvar);
    split(x, bx, tx, x_size);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(tx, device_api);
    parallel(bx);
    parallel(tx);
    return *this;
}


Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       Expr x_size, Expr y_size,
                       DeviceAPI device_api) {
    VarOrRVar bx("__block_id_x", x.is_rvar),
        by("__block_id_y", y.is_rvar),
        tx("__thread_id_x", x.is_rvar),
        ty("__thread_id_y", y.is_rvar);
    tile(x, y, bx, by, tx, ty, x_size, y_size);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    parallel(bx);
    parallel(by);
    parallel(tx);
    parallel(ty);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                       Expr x_size, Expr y_size, Expr z_size,
                       DeviceAPI device_api) {
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
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_device_api(bz, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_device_api(tz, device_api);
    parallel(bx);
    parallel(by);
    parallel(bz);
    parallel(tx);
    parallel(ty);
    parallel(tz);
    return *this;
}

void Func::invalidate_cache() {
    if (pipeline_.defined()) {
        pipeline_.invalidate_cache();
    }
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

Func &Func::gpu_threads(VarOrRVar tx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, device_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, ty, device_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_threads(tx, ty, tz, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, by, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_blocks(bx, by, bz, device_api);
    return *this;
}

Func &Func::gpu_single_thread(DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_single_thread(device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar tx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, tx, device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, by, tx, ty, device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu(bx, by, bz, tx, ty, tz, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, int x_size, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, x_size, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, int x_size, int y_size, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, y, x_size, y_size, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z, int x_size, int y_size, int z_size, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.schedule(), name()).gpu_tile(x, y, z, x_size, y_size, z_size, device_api);
    return *this;
}

Func &Func::glsl(Var x, Var y, Var c) {
    invalidate_cache();

    reorder(c, x, y);
    // GLSL outputs must be stored interleaved
    reorder_storage(c, x, y);

    // TODO: Set appropriate constraints if this is the output buffer?

    Stage(func.schedule(), name()).gpu_blocks(x, y, DeviceAPI::GLSL);

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

Func &Func::reorder_storage(const std::vector<Var> &dims, size_t start) {
    // Reorder the first dimension with respect to all others, then
    // recursively reorder all remaining dimensions.
    for (size_t i = start + 1; i < dims.size(); i++) {
        reorder_storage(dims[start], dims[i]);
    }
    if ((dims.size() - start) > 2) {
        reorder_storage(dims, start + 1);
    }
    return *this;
}

Func &Func::reorder_storage(const std::vector<Var> &dims) {
    user_assert(dims.size() > 1) <<
        "reorder_storage must have at least two dimensions in reorder list.\n";

    return reorder_storage(dims, 0);
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

    size_t update_stage = func.updates().size() - 1;
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

Pipeline Func::pipeline() {
    if (!pipeline_.defined()) {
        pipeline_ = Pipeline(*this);
    }
    internal_assert(pipeline_.defined());
    return pipeline_;
}

vector<Argument> Func::infer_arguments() const {
    return Pipeline(*this).infer_arguments();
}

Module Func::compile_to_module(const vector<Argument> &args, const std::string &fn_name, const Target &target) {
    return pipeline().compile_to_module(args, fn_name, target);
}


void Func::compile_to(const Outputs &output_files,
                      const vector<Argument> &args,
                      const string &fn_name,
                      const Target &target) {
    pipeline().compile_to(output_files, args, fn_name, target);
}

void Func::compile_to_bitcode(const string &filename, const vector<Argument> &args, const string &fn_name,
                              const Target &target) {
    pipeline().compile_to_bitcode(filename, args, fn_name, target);
}

void Func::compile_to_bitcode(const string &filename, const vector<Argument> &args,
                              const Target &target) {
    pipeline().compile_to_bitcode(filename, args, "", target);
}

void Func::compile_to_object(const string &filename, const vector<Argument> &args,
                             const string &fn_name, const Target &target) {
    pipeline().compile_to_object(filename, args, fn_name, target);
}

void Func::compile_to_object(const string &filename, const vector<Argument> &args,
                             const Target &target) {
    pipeline().compile_to_object(filename, args, "", target);
}

void Func::compile_to_header(const string &filename, const vector<Argument> &args,
                             const string &fn_name, const Target &target) {
    pipeline().compile_to_header(filename, args, fn_name, target);
}

void Func::compile_to_c(const string &filename, const vector<Argument> &args,
                        const string &fn_name, const Target &target) {
    pipeline().compile_to_c(filename, args, fn_name, target);
}

void Func::compile_to_lowered_stmt(const string &filename,
                                   const vector<Argument> &args,
                                   StmtOutputFormat fmt,
                                   const Target &target) {
    pipeline().compile_to_lowered_stmt(filename, args, fmt, target);
}

void Func::print_loop_nest() {
    pipeline().print_loop_nest();
}

void Func::compile_to_file(const string &filename_prefix,
                           const vector<Argument> &args,
                           const Target &target) {
    pipeline().compile_to_file(filename_prefix, args, target);
}

void Func::compile_to_assembly(const string &filename, const vector<Argument> &args, const string &fn_name,
                               const Target &target) {
    pipeline().compile_to_assembly(filename, args, fn_name, target);
}

void Func::compile_to_assembly(const string &filename, const vector<Argument> &args, const Target &target) {
    pipeline().compile_to_assembly(filename, args, "", target);
}

// JIT-related code

void Func::set_error_handler(void (*handler)(void *, const char *)) {
    pipeline().set_error_handler(handler);
}

void Func::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                void (*cust_free)(void *, void *)) {
    pipeline().set_custom_allocator(cust_malloc, cust_free);
}

void Func::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    pipeline().set_custom_do_par_for(cust_do_par_for);
}

void Func::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    pipeline().set_custom_do_task(cust_do_task);
}

void Func::set_custom_trace(int (*trace_fn)(void *, const halide_trace_event *)) {
    pipeline().set_custom_trace(trace_fn);
}

void Func::set_custom_print(void (*cust_print)(void *, const char *)) {
    pipeline().set_custom_print(cust_print);
}

void Func::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
    pipeline().add_custom_lowering_pass(pass, deleter);
}

void Func::clear_custom_lowering_passes() {
    pipeline().clear_custom_lowering_passes();
}

const vector<CustomLoweringPass> &Func::custom_lowering_passes() {
    return pipeline().custom_lowering_passes();
}

const Internal::JITHandlers &Func::jit_handlers() {
    return pipeline().jit_handlers();
}

void Func::realize(Buffer b, const Target &target) {
    pipeline().realize(b, target);
}

void Func::realize(Realization dst, const Target &target) {
    pipeline().realize(dst, target);
}

void Func::infer_input_bounds(Buffer dst) {
    pipeline().infer_input_bounds(dst);
}

void Func::infer_input_bounds(Realization dst) {
    pipeline().infer_input_bounds(dst);
}

void *Func::compile_jit(const Target &target) {
    return pipeline().compile_jit(target);
}

EXPORT Var _("_");
EXPORT Var _0("_0"), _1("_1"), _2("_2"), _3("_3"), _4("_4"),
           _5("_5"), _6("_6"), _7("_7"), _8("_8"), _9("_9");

}
