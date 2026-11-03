#include "PrecomputeConstants.h"

#include <cstring>
#include <set>
#include <sstream>
#include <utility>

#include "Argument.h"
#include "Buffer.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Module.h"
#include "Parameter.h"
#include "Pipeline.h"
#include "Realization.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

// Calls visitor on every Expr that determines the values a definition
// computes. Scheduling-only Exprs (split factors, specialization conditions,
// ...) are skipped, because the definition is evaluated with a default schedule.
void visit_definition_values(const Definition &def, IRVisitor *visitor) {
    if (def.predicate().defined()) {
        def.predicate().accept(visitor);
    }
    for (const Expr &e : def.args()) {
        e.accept(visitor);
    }
    for (const Expr &e : def.values()) {
        e.accept(visitor);
    }
    for (const ReductionVariable &rv : def.schedule().rvars()) {
        rv.min.accept(visitor);
        rv.extent.accept(visitor);
    }
}

void visit_function_values(const Function &f, IRVisitor *visitor) {
    visit_definition_values(f.definition(), visitor);
    for (const Definition &def : f.updates()) {
        visit_definition_values(def, visitor);
    }
}

// If f is the Func that wraps an ImageParam, i.e. its only definition reads
// the ImageParam at f's own coordinates, returns that ImageParam's Parameter.
Parameter image_param_wrapped_by(const Function &f) {
    if (f.has_extern_definition() || !f.has_pure_definition() || f.has_update_definition() ||
        f.values().size() != 1) {
        return Parameter();
    }
    const Call *call = f.values()[0].as<Call>();
    if (!call || !call->param.defined() || !call->param.is_buffer() ||
        call->args.size() != f.args().size()) {
        return Parameter();
    }
    for (size_t i = 0; i < call->args.size(); i++) {
        const Variable *v = call->args[i].as<Variable>();
        if (!v || v->name != f.args()[i]) {
            return Parameter();
        }
    }
    return call->param;
}

// Finds the first thing an Expr reads that isn't known at compile time.
class FindNonConstant : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const set<string> &input_buffers;

    void found(const string &what) {
        if (reason.empty()) {
            reason = what;
        }
    }

    void visit(const Variable *op) override {
        if (op->param.defined()) {
            found(string(op->param.is_buffer() ? "the ImageParam \"" : "the Param \"") +
                  op->param.name() + "\"");
        } else if (op->image.defined() && input_buffers.count(op->image.name())) {
            found("the input Buffer \"" + op->image.name() + "\"");
        }
        IRGraphVisitor::visit(op);
    }

    void visit(const Call *op) override {
        if (op->param.defined()) {
            found("the ImageParam \"" + op->param.name() + "\"");
        } else if (op->image.defined() && input_buffers.count(op->image.name())) {
            found("the input Buffer \"" + op->image.name() + "\"");
        } else if (op->call_type == Call::Extern ||
                   op->call_type == Call::ExternCPlusPlus) {
            found("the result of the extern function \"" + op->name + "\", which may not be pure");
        } else if (op->call_type == Call::Halide && op->func.defined()) {
            // Report a read of an ImageParam's wrapper Func as a read of the
            // ImageParam itself.
            const Parameter p = image_param_wrapped_by(Function(op->func));
            if (p.defined()) {
                found("the ImageParam \"" + p.name() + "\"");
            }
        }
        IRGraphVisitor::visit(op);
    }

public:
    string reason;

    explicit FindNonConstant(const set<string> &input_buffers)
        : input_buffers(input_buffers) {
    }
};

// Checks that f, and every Func it calls, can be evaluated at compile time.
void check_constant(const Function &f,
                    const map<string, Function> &sub_env,
                    const set<string> &input_buffers) {
    vector<Function> funcs = {f};
    for (const auto &[name, g] : sub_env) {
        // ImageParam wrappers are reported by the Funcs that call them.
        if (name != f.name() && !image_param_wrapped_by(g).defined()) {
            funcs.push_back(g);
        }
    }
    for (const Function &g : funcs) {
        std::ostringstream who;
        if (g.same_as(f)) {
            who << "it";
        } else {
            who << "the Func \"" << g.name() << "\", which it depends on,";
        }
        if (g.has_extern_definition()) {
            user_error << "Func \"" << f.name() << "\" is scheduled precompute(), but "
                       << (g.same_as(f) ? "it has" : who.str() + " has")
                       << " an extern definition, so it cannot be evaluated at compile time.\n";
        }
        FindNonConstant finder(input_buffers);
        visit_function_values(g, &finder);
        if (!finder.reason.empty()) {
            user_error << "Func \"" << f.name() << "\" is scheduled precompute(), but "
                       << who.str() << " reads " << finder.reason
                       << ", so it cannot be evaluated at compile time.\n";
        }
    }
}

// Does the Func graph compute any floating-point value, and does it use
// strict_float?
class FindFloats : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    void include(const Expr &e) override {
        if (e.type().is_float()) {
            any_float = true;
        }
        IRGraphVisitor::include(e);
    }

    void visit(const Call *op) override {
        if (op->is_strict_float_intrinsic()) {
            any_strict_float = true;
        }
        IRGraphVisitor::visit(op);
    }

public:
    bool any_float = false;
    bool any_strict_float = false;

    void check(const Expr &e) {
        if (e.defined()) {
            include(e);
        }
    }
};

void check_function_floats(const Function &f, FindFloats &finder) {
    auto check_definition = [&](const Definition &def) {
        finder.check(def.predicate());
        for (const Expr &e : def.args()) {
            finder.check(e);
        }
        for (const Expr &e : def.values()) {
            finder.check(e);
        }
        for (const ReductionVariable &rv : def.schedule().rvars()) {
            finder.check(rv.min);
            finder.check(rv.extent);
        }
    };
    check_definition(f.definition());
    for (const Definition &def : f.updates()) {
        check_definition(def);
    }
}

// Give a Function, and each of its definitions, the schedule it would have
// had if it had never been scheduled.
void reset_schedule(Function &f) {
    FuncSchedule func_schedule;
    for (const string &arg : f.args()) {
        func_schedule.storage_dims().push_back(StorageDim{arg});
    }
    f.schedule() = func_schedule;

    auto reset_stage = [&](Definition &def) {
        StageSchedule stage_schedule;
        stage_schedule.rvars() = def.schedule().rvars();
        for (const ReductionVariable &rv : stage_schedule.rvars()) {
            stage_schedule.dims().push_back(
                Dim{rv.var, ForType::Serial, DeviceAPI::None, DimType::ImpureRVar});
        }
        for (size_t i = 0; i < def.args().size(); i++) {
            const Variable *v = def.args()[i].as<Variable>();
            if (v && !v->param.defined() && !v->reduction_domain.defined() &&
                v->name == f.args()[i]) {
                stage_schedule.dims().push_back(
                    Dim{v->name, ForType::Serial, DeviceAPI::None, DimType::PureVar});
            }
        }
        stage_schedule.dims().push_back(
            Dim{Var::outermost().name(), ForType::Serial, DeviceAPI::None, DimType::PureVar});
        def.schedule() = stage_schedule;
        def.specializations().clear();
    };
    reset_stage(f.definition());
    for (size_t i = 0; i < f.updates().size(); i++) {
        reset_stage(f.update((int)i));
    }
}

// JIT-compile a fresh copy of f, and the Funcs it calls, with default
// schedules, and realize it on the host over the given region.
vector<Buffer<>> evaluate(const Function &f,
                          const map<string, Function> &sub_env,
                          const vector<int> &mins,
                          const vector<int> &extents) {
    auto [outputs, env] = deep_copy({f}, sub_env);
    for (auto &[name, g] : env) {
        reset_schedule(g);
        // The Func is evaluated at compile time, so its tracing and
        // debug_to_file directives would fire then rather than at run time.
        g.clear_tracing();
        g.debug_file().clear();
    }

    vector<Buffer<>> buffers;
    for (const Type &t : f.output_types()) {
        Buffer<> b(t, extents);
        b.set_min(mins);
        buffers.push_back(b);
    }

    Pipeline p{Func(outputs[0])};
    p.realize(Realization(buffers), get_host_target());
    return buffers;
}

bool same_contents(const Buffer<> &a, const Buffer<> &b) {
    // Zero-dimensional embedded Buffers are emitted as mutable globals (to
    // hold stateful module information), so they are never shared.
    if (a.dimensions() == 0 || b.dimensions() == 0) {
        return false;
    }
    if (a.type() != b.type() ||
        a.dimensions() != b.dimensions()) {
        return false;
    }
    for (int i = 0; i < a.dimensions(); i++) {
        if (a.dim(i).min() != b.dim(i).min() ||
            a.dim(i).extent() != b.dim(i).extent() ||
            a.dim(i).stride() != b.dim(i).stride()) {
            return false;
        }
    }
    // Only dense Buffers are compared byte-for-byte. Buffers made by this
    // pass are dense; a non-dense one is just treated as distinct.
    int64_t dense_stride = 1;
    for (int i = 0; i < a.dimensions(); i++) {
        if (a.dim(i).stride() != dense_stride) {
            return false;
        }
        dense_stride *= a.dim(i).extent();
    }
    if (!a.data() || !b.data()) {
        return false;
    }
    return a.data() == b.data() ||
           std::memcmp(a.data(), b.data(), a.size_in_bytes()) == 0;
}

// Finds the Realize of each precomputed Func, and resolves its bounds through
// the LetStmts that enclose it.
class FindRegions : public IRVisitor {
    using IRVisitor::visit;

    const map<string, Function> &funcs;
    vector<const LetStmt *> lets;

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        lets.push_back(op);
        op->body.accept(this);
        lets.pop_back();
    }

    Expr resolve(Expr e) const {
        for (auto it = lets.rbegin(); it != lets.rend(); ++it) {
            if (expr_uses_var(e, (*it)->name)) {
                e = Let::make((*it)->name, (*it)->value, e);
            }
        }
        return simplify(e);
    }

    void visit(const Realize *op) override {
        if (funcs.count(op->name)) {
            Region region;
            for (const Range &r : op->bounds) {
                region.emplace_back(resolve(r.min), resolve(r.extent));
            }
            regions.emplace(op->name, std::move(region));
        }
        IRVisitor::visit(op);
    }

public:
    map<string, Region> regions;

    explicit FindRegions(const map<string, Function> &funcs)
        : funcs(funcs) {
    }
};

// Removes the realizations of precomputed Funcs, and redirects calls to them
// to the Buffers that hold their values.
class ReplaceWithBuffers : public IRMutator {
    using IRMutator::visit;

    const map<string, vector<Buffer<>>> &buffers;

    Stmt visit(const Realize *op) override {
        if (buffers.count(op->name)) {
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (buffers.count(op->name)) {
            if (op->is_producer) {
                return Evaluate::make(0);
            }
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Prefetch *op) override {
        auto it = buffers.find(op->name);
        if (it == buffers.end()) {
            return IRMutator::visit(op);
        }
        Stmt body = mutate(op->body);
        for (const Buffer<> &b : it->second) {
            PrefetchDirective p = op->prefetch;
            p.name = b.name();
            body = Prefetch::make(b.name(), {b.type()}, op->bounds, p, op->condition, body);
        }
        return body;
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::Halide) {
            auto it = buffers.find(op->name);
            if (it != buffers.end()) {
                internal_assert(op->value_index < (int)it->second.size());
                return Call::make(it->second[op->value_index], mutate(op->args));
            }
        }
        return IRMutator::visit(op);
    }

public:
    using IRMutator::mutate;

    explicit ReplaceWithBuffers(const map<string, vector<Buffer<>>> &buffers)
        : buffers(buffers) {
    }
};

}  // namespace

Stmt precompute_constants(const Stmt &s,
                          const vector<string> &order,
                          const map<string, Function> &env,
                          const vector<Argument> &args,
                          const Target &t,
                          const Module &result_module) {
    map<string, Function> precomputed;
    for (const string &name : order) {
        const Function &f = env.at(name);
        if (f.schedule().precomputed()) {
            precomputed.emplace(name, f);
        }
    }
    if (precomputed.empty()) {
        return s;
    }

    set<string> input_buffers;
    for (const Argument &arg : args) {
        if (arg.is_buffer()) {
            input_buffers.insert(arg.name);
        }
    }

    for (const auto &[name, g] : env) {
        if (!g.has_extern_definition()) {
            continue;
        }
        for (const ExternFuncArgument &arg : g.extern_arguments()) {
            user_assert(!(arg.is_func() && precomputed.count(Function(arg.func).name())))
                << "Func \"" << Function(arg.func).name() << "\" is scheduled precompute(), "
                << "but it is consumed by the extern stage \"" << name << "\". "
                << "A precomputed Func can only be read by Halide definitions.\n";
        }
    }

    FindRegions finder(precomputed);
    s.accept(&finder);

    const Target host = get_host_target();
    const bool cross_compiling = t.arch != host.arch || t.bits != host.bits || t.os != host.os;

    // Every Buffer embedded so far, for deduplication.
    vector<Buffer<>> known = result_module.buffers();

    map<string, vector<Buffer<>>> replacements;
    for (const string &name : order) {
        auto pc = precomputed.find(name);
        if (pc == precomputed.end()) {
            continue;
        }
        const Function &f = pc->second;
        const map<string, Function> sub_env = build_environment({f});
        check_constant(f, sub_env, input_buffers);

        auto region = finder.regions.find(name);
        if (region == finder.regions.end()) {
            // f is never used, so it's never realized.
            continue;
        }

        vector<int> mins, extents;
        for (size_t i = 0; i < region->second.size(); i++) {
            const Range &r = region->second[i];
            const auto min = as_const_int(r.min);
            const auto extent = as_const_int(r.extent);
            if (!min || !extent) {
                user_error << "Func \"" << name << "\" is scheduled precompute(), but the region "
                           << "of it that is used is not known at compile time. In dimension "
                           << i << " (\"" << f.args()[i] << "\"), the min is "
                           << simplify(substitute_in_all_lets(r.min)) << " and the extent is "
                           << simplify(substitute_in_all_lets(r.extent)) << ". Use "
                           << "Func::bound() to fix the region to a constant.\n";
            }
            user_assert(*extent > 0)
                << "Func \"" << name << "\" is scheduled precompute(), but the region of "
                << "it that is used is empty in dimension " << i << " (\"" << f.args()[i] << "\").\n";
            mins.push_back((int)*min);
            extents.push_back((int)*extent);
        }

        if (cross_compiling && !t.has_feature(Target::StrictFloat)) {
            FindFloats floats;
            for (const auto &[n, g] : sub_env) {
                check_function_floats(g, floats);
            }
            if (floats.any_float && !floats.any_strict_float) {
                user_warning
                    << "Func \"" << name << "\" is scheduled precompute() and computes "
                    << "floating-point values. It is evaluated on the host (" << host.to_string()
                    << "), which may not compute exactly what the target (" << t.to_string()
                    << ") would, e.g. for transcendental functions or fast_* intrinsics. "
                    << "Use strict_float to silence this warning.\n";
            }
        }

        debug(1) << "Precomputing " << name << " with mins " << PrintSpan(mins)
                 << " and extents " << PrintSpan(extents) << "\n";
        vector<Buffer<>> buffers = evaluate(f, sub_env, mins, extents);

        for (size_t i = 0; i < buffers.size(); i++) {
            Buffer<> &b = buffers[i];
            bool reused = false;
            for (const Buffer<> &k : known) {
                if (same_contents(b, k)) {
                    debug(1) << "Reusing identical precomputed Buffer " << k.name() << "\n";
                    b = k;
                    reused = true;
                    break;
                }
            }
            if (!reused) {
                string base = name;
                if (buffers.size() > 1) {
                    base += "_" + std::to_string(i);
                }
                b.set_name(unique_name(base + "_precomputed"));
                known.push_back(b);
            }
        }
        replacements.emplace(name, std::move(buffers));
    }

    if (replacements.empty()) {
        return s;
    }
    return ReplaceWithBuffers(replacements).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
