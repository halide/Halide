#include "ImplementWithMatcher.h"

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "IRVisitor.h"
#include "Lower.h"
#include "Pipeline.h"
#include "RealizationOrder.h"
#include "SimplifySpecializations.h"
#include "StrictifyFloat.h"
#include "Target.h"
#include "TargetQueryOps.h"
#include "WrapCalls.h"

namespace Halide {
namespace Internal {

Stmt lower_spec_to_canonical_form(const Pipeline &spec, const Target &t) {
    // Extract the Internal::Function handles from the spec's outputs.
    std::vector<Function> output_funcs;
    for (const Func &f : spec.outputs()) {
        Function fn = f.function();
        internal_assert(fn.is_spec_pattern())
            << "lower_spec_to_canonical_form expects every output of the "
               "spec Pipeline to be a spec-pattern Func (one produced by "
               "Instruction::spec()); got output '"
            << fn.name() << "' which is not marked spec-pattern.";
        output_funcs.push_back(fn);
    }

    // Mirror lower_impl's pre-canonical-form setup. Two intentional
    // omissions compared to lower_impl:
    //   - apply_implement_with_directives: specs do not themselves carry
    //     implement_with directives, so there is nothing to apply.
    //   - any-strict-float flag plumbing into a Module: we never produce
    //     a Module here, so the bool return of strictify_float is dropped.
    auto [outputs, env] =
        deep_copy(output_funcs, build_environment(output_funcs));

    lower_target_query_ops(env, t);
    (void)strictify_float(env, t);

    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    env = wrap_func_calls(env);

    auto [order, fused_groups] = realization_order(outputs, env);

    simplify_specializations(env);

    return lower_to_canonical_form(outputs, env, order, fused_groups, t,
                                   /*requirements=*/{},
                                   /*pipeline_name=*/"implement_with_spec",
                                   /*trace_pipeline=*/false);
}

namespace {

// IRVisitor that records the first For node whose name matches the
// target. The canonical-form prefix runs uniquify_variable_names, so a
// given For name appears at most once in the Stmt.
class FindForByName : public IRVisitor {
public:
    Stmt found;
    const std::string &target_name;

    explicit FindForByName(const std::string &name) : target_name(name) {
    }

    using IRVisitor::visit;

    void visit(const For *op) override {
        if (op->name == target_name) {
            found = Stmt(op);
            return;
        }
        IRVisitor::visit(op);
    }
};

}  // namespace

Stmt find_implement_with_loop(const Stmt &s,
                              const std::string &user_func_name,
                              int stage_index,
                              const std::string &loop_var_name) {
    std::string target = user_func_name + ".s" +
                         std::to_string(stage_index) + "." +
                         loop_var_name;
    FindForByName v(target);
    s.accept(&v);
    return v.found;
}

namespace {

// Parallel-walk matcher. Recurses through paired Stmt/Expr trees,
// binding spec-side names to user-side names in two maps:
//   var_rename:  Variables, For loop vars, Let{,Stmt} bound names
//   func_rename: Buffer/Func/intrinsic names in Load, Store, Call,
//                Provide, Realize, Allocate, Free, ProducerConsumer,
//                Atomic, HoistedStorage, Prefetch
// Commutative ops (Add, Mul, Min, Max) try both child orderings and
// roll back binding mutations on first-attempt failure.
class Matcher {
public:
    std::map<std::string, std::string> var_rename;
    std::map<std::string, std::string> func_rename;
    std::string failure_reason;

    bool match_stmt(const Stmt &a, const Stmt &b);
    bool match_expr(const Expr &a, const Expr &b);

private:
    bool bind(std::map<std::string, std::string> &m,
              const std::string &spec_name,
              const std::string &user_name,
              const char *kind);

    bool match_type(const Type &a, const Type &b, const char *context);
    bool match_exprs(const std::vector<Expr> &a,
                     const std::vector<Expr> &b,
                     const char *context);

    template<typename F1, typename F2>
    bool try_either(F1 first, F2 second);

    bool visit(const IntImm *a, const IntImm *b);
    bool visit(const UIntImm *a, const UIntImm *b);
    bool visit(const FloatImm *a, const FloatImm *b);
    bool visit(const StringImm *a, const StringImm *b);
    bool visit(const Cast *a, const Cast *b);
    bool visit(const Reinterpret *a, const Reinterpret *b);
    bool visit(const Variable *a, const Variable *b);
    bool visit(const Add *a, const Add *b);
    bool visit(const Sub *a, const Sub *b);
    bool visit(const Mul *a, const Mul *b);
    bool visit(const Div *a, const Div *b);
    bool visit(const Mod *a, const Mod *b);
    bool visit(const Min *a, const Min *b);
    bool visit(const Max *a, const Max *b);
    bool visit(const EQ *a, const EQ *b);
    bool visit(const NE *a, const NE *b);
    bool visit(const LT *a, const LT *b);
    bool visit(const LE *a, const LE *b);
    bool visit(const GT *a, const GT *b);
    bool visit(const GE *a, const GE *b);
    bool visit(const And *a, const And *b);
    bool visit(const Or *a, const Or *b);
    bool visit(const Not *a, const Not *b);
    bool visit(const Select *a, const Select *b);
    bool visit(const Load *a, const Load *b);
    bool visit(const Ramp *a, const Ramp *b);
    bool visit(const Broadcast *a, const Broadcast *b);
    bool visit(const Call *a, const Call *b);
    bool visit(const Let *a, const Let *b);
    bool visit(const Shuffle *a, const Shuffle *b);
    bool visit(const VectorReduce *a, const VectorReduce *b);

    bool visit(const LetStmt *a, const LetStmt *b);
    bool visit(const AssertStmt *a, const AssertStmt *b);
    bool visit(const ProducerConsumer *a, const ProducerConsumer *b);
    bool visit(const For *a, const For *b);
    bool visit(const Acquire *a, const Acquire *b);
    bool visit(const Store *a, const Store *b);
    bool visit(const Provide *a, const Provide *b);
    bool visit(const Allocate *a, const Allocate *b);
    bool visit(const Free *a, const Free *b);
    bool visit(const Realize *a, const Realize *b);
    bool visit(const Block *a, const Block *b);
    bool visit(const Fork *a, const Fork *b);
    bool visit(const IfThenElse *a, const IfThenElse *b);
    bool visit(const Evaluate *a, const Evaluate *b);
    bool visit(const Prefetch *a, const Prefetch *b);
    bool visit(const Atomic *a, const Atomic *b);
    bool visit(const HoistedStorage *a, const HoistedStorage *b);
};

bool Matcher::bind(std::map<std::string, std::string> &m,
                   const std::string &spec_name,
                   const std::string &user_name,
                   const char *kind) {
    auto [it, inserted] = m.emplace(spec_name, user_name);
    if (!inserted && it->second != user_name) {
        std::ostringstream os;
        os << kind << " name binding conflict: spec '" << spec_name
           << "' was previously bound to user '" << it->second
           << "' but is now seen as user '" << user_name << "'.";
        failure_reason = os.str();
        return false;
    }
    return true;
}

bool Matcher::match_type(const Type &a, const Type &b, const char *context) {
    if (a != b) {
        std::ostringstream os;
        os << context << ": type mismatch (spec=" << a << ", user=" << b
           << ").";
        failure_reason = os.str();
        return false;
    }
    return true;
}

bool Matcher::match_exprs(const std::vector<Expr> &a,
                          const std::vector<Expr> &b,
                          const char *context) {
    if (a.size() != b.size()) {
        std::ostringstream os;
        os << context << ": arity mismatch (spec=" << a.size()
           << ", user=" << b.size() << ").";
        failure_reason = os.str();
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!match_expr(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

template<typename F1, typename F2>
bool Matcher::try_either(F1 first, F2 second) {
    auto snap_v = var_rename;
    auto snap_f = func_rename;
    auto snap_r = failure_reason;
    if (first()) {
        return true;
    }
    var_rename = std::move(snap_v);
    func_rename = std::move(snap_f);
    failure_reason = std::move(snap_r);
    return second();
}

bool Matcher::match_expr(const Expr &a, const Expr &b) {
    if (!a.defined() && !b.defined()) {
        return true;
    }
    if (!a.defined() || !b.defined()) {
        failure_reason = "one Expr is defined and the other is not.";
        return false;
    }
    if (a->node_type != b->node_type) {
        std::ostringstream os;
        os << "Expr node type mismatch (spec="
           << IRNodeType_string(a->node_type)
           << ", user=" << IRNodeType_string(b->node_type) << ").";
        failure_reason = os.str();
        return false;
    }
    if (!match_type(a.type(), b.type(), "Expr")) {
        return false;
    }
    switch (a->node_type) {
#define HANDLE_EXPR(T)                                  \
    case IRNodeType::T:                                 \
        return visit(static_cast<const T *>(a.get()),   \
                     static_cast<const T *>(b.get()));
        HALIDE_FOR_EACH_IR_EXPR(HANDLE_EXPR)
#undef HANDLE_EXPR
    default:
        failure_reason = "Expr node kind not handled by matcher.";
        return false;
    }
}

bool Matcher::match_stmt(const Stmt &a, const Stmt &b) {
    if (!a.defined() && !b.defined()) {
        return true;
    }
    if (!a.defined() || !b.defined()) {
        failure_reason = "one Stmt is defined and the other is not.";
        return false;
    }
    if (a->node_type != b->node_type) {
        std::ostringstream os;
        os << "Stmt node type mismatch (spec="
           << IRNodeType_string(a->node_type)
           << ", user=" << IRNodeType_string(b->node_type) << ").";
        failure_reason = os.str();
        return false;
    }
    switch (a->node_type) {
#define HANDLE_STMT(T)                                  \
    case IRNodeType::T:                                 \
        return visit(static_cast<const T *>(a.get()),   \
                     static_cast<const T *>(b.get()));
        HALIDE_FOR_EACH_IR_STMT(HANDLE_STMT)
#undef HANDLE_STMT
    default:
        failure_reason = "Stmt node kind not handled by matcher.";
        return false;
    }
}

bool Matcher::visit(const IntImm *a, const IntImm *b) {
    if (a->value != b->value) {
        failure_reason = "IntImm value mismatch.";
        return false;
    }
    return true;
}

bool Matcher::visit(const UIntImm *a, const UIntImm *b) {
    if (a->value != b->value) {
        failure_reason = "UIntImm value mismatch.";
        return false;
    }
    return true;
}

bool Matcher::visit(const FloatImm *a, const FloatImm *b) {
    if (a->value != b->value) {
        failure_reason = "FloatImm value mismatch.";
        return false;
    }
    return true;
}

bool Matcher::visit(const StringImm *a, const StringImm *b) {
    if (a->value != b->value) {
        failure_reason = "StringImm value mismatch.";
        return false;
    }
    return true;
}

bool Matcher::visit(const Cast *a, const Cast *b) {
    return match_expr(a->value, b->value);
}

bool Matcher::visit(const Reinterpret *a, const Reinterpret *b) {
    return match_expr(a->value, b->value);
}

bool Matcher::visit(const Variable *a, const Variable *b) {
    return bind(var_rename, a->name, b->name, "Variable");
}

bool Matcher::visit(const Not *a, const Not *b) {
    return match_expr(a->a, b->a);
}

#define NON_COMMUTATIVE_BINOP(NodeT)                                   \
    bool Matcher::visit(const NodeT *a, const NodeT *b) {              \
        return match_expr(a->a, b->a) && match_expr(a->b, b->b);       \
    }

#define COMMUTATIVE_BINOP(NodeT)                                       \
    bool Matcher::visit(const NodeT *a, const NodeT *b) {              \
        return try_either(                                             \
            [&] { return match_expr(a->a, b->a) &&                     \
                         match_expr(a->b, b->b); },                    \
            [&] { return match_expr(a->a, b->b) &&                     \
                         match_expr(a->b, b->a); });                   \
    }

COMMUTATIVE_BINOP(Add)
COMMUTATIVE_BINOP(Mul)
COMMUTATIVE_BINOP(Min)
COMMUTATIVE_BINOP(Max)
NON_COMMUTATIVE_BINOP(Sub)
NON_COMMUTATIVE_BINOP(Div)
NON_COMMUTATIVE_BINOP(Mod)
NON_COMMUTATIVE_BINOP(EQ)
NON_COMMUTATIVE_BINOP(NE)
NON_COMMUTATIVE_BINOP(LT)
NON_COMMUTATIVE_BINOP(LE)
NON_COMMUTATIVE_BINOP(GT)
NON_COMMUTATIVE_BINOP(GE)
NON_COMMUTATIVE_BINOP(And)
NON_COMMUTATIVE_BINOP(Or)

#undef COMMUTATIVE_BINOP
#undef NON_COMMUTATIVE_BINOP

bool Matcher::visit(const Select *a, const Select *b) {
    return match_expr(a->condition, b->condition) &&
           match_expr(a->true_value, b->true_value) &&
           match_expr(a->false_value, b->false_value);
}

bool Matcher::visit(const Load *a, const Load *b) {
    return bind(func_rename, a->name, b->name, "Load buffer") &&
           match_expr(a->predicate, b->predicate) &&
           match_expr(a->index, b->index);
}

bool Matcher::visit(const Ramp *a, const Ramp *b) {
    if (a->lanes != b->lanes) {
        failure_reason = "Ramp lanes mismatch.";
        return false;
    }
    return match_expr(a->base, b->base) && match_expr(a->stride, b->stride);
}

bool Matcher::visit(const Broadcast *a, const Broadcast *b) {
    if (a->lanes != b->lanes) {
        failure_reason = "Broadcast lanes mismatch.";
        return false;
    }
    return match_expr(a->value, b->value);
}

bool Matcher::visit(const Call *a, const Call *b) {
    if (a->call_type != b->call_type) {
        failure_reason = "Call call_type mismatch.";
        return false;
    }
    // Intrinsic and extern names are stable strings; Halide-call names
    // are Func names that participate in alpha-renaming. Both route
    // through func_rename: stable strings just record identity
    // bindings.
    if (!bind(func_rename, a->name, b->name, "Call name")) {
        return false;
    }
    return match_exprs(a->args, b->args, "Call args");
}

bool Matcher::visit(const Let *a, const Let *b) {
    return bind(var_rename, a->name, b->name, "Let") &&
           match_expr(a->value, b->value) &&
           match_expr(a->body, b->body);
}

bool Matcher::visit(const Shuffle *a, const Shuffle *b) {
    if (a->indices != b->indices) {
        failure_reason = "Shuffle indices mismatch.";
        return false;
    }
    return match_exprs(a->vectors, b->vectors, "Shuffle vectors");
}

bool Matcher::visit(const VectorReduce *a, const VectorReduce *b) {
    if (a->op != b->op) {
        failure_reason = "VectorReduce op mismatch.";
        return false;
    }
    return match_expr(a->value, b->value);
}

bool Matcher::visit(const LetStmt *a, const LetStmt *b) {
    return bind(var_rename, a->name, b->name, "LetStmt") &&
           match_expr(a->value, b->value) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const AssertStmt *a, const AssertStmt *b) {
    return match_expr(a->condition, b->condition) &&
           match_expr(a->message, b->message);
}

bool Matcher::visit(const ProducerConsumer *a, const ProducerConsumer *b) {
    if (a->is_producer != b->is_producer) {
        failure_reason =
            "ProducerConsumer kind mismatch (produce vs consume).";
        return false;
    }
    return bind(func_rename, a->name, b->name, "ProducerConsumer") &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const For *a, const For *b) {
    if (a->for_type != b->for_type) {
        failure_reason = "For for_type mismatch.";
        return false;
    }
    if (a->device_api != b->device_api) {
        failure_reason = "For device_api mismatch.";
        return false;
    }
    if (a->partition_policy != b->partition_policy) {
        failure_reason = "For partition_policy mismatch.";
        return false;
    }
    return bind(var_rename, a->name, b->name, "For loop var") &&
           match_expr(a->min, b->min) &&
           match_expr(a->max, b->max) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const Acquire *a, const Acquire *b) {
    return match_expr(a->semaphore, b->semaphore) &&
           match_expr(a->count, b->count) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const Store *a, const Store *b) {
    return bind(func_rename, a->name, b->name, "Store buffer") &&
           match_expr(a->predicate, b->predicate) &&
           match_expr(a->value, b->value) &&
           match_expr(a->index, b->index);
}

bool Matcher::visit(const Provide *a, const Provide *b) {
    return bind(func_rename, a->name, b->name, "Provide") &&
           match_exprs(a->values, b->values, "Provide values") &&
           match_exprs(a->args, b->args, "Provide args") &&
           match_expr(a->predicate, b->predicate);
}

bool Matcher::visit(const Allocate *a, const Allocate *b) {
    if (a->memory_type != b->memory_type) {
        failure_reason = "Allocate memory_type mismatch.";
        return false;
    }
    if (a->padding != b->padding) {
        failure_reason = "Allocate padding mismatch.";
        return false;
    }
    return bind(func_rename, a->name, b->name, "Allocate") &&
           match_type(a->type, b->type, "Allocate") &&
           match_exprs(a->extents, b->extents, "Allocate extents") &&
           match_expr(a->condition, b->condition) &&
           match_expr(a->new_expr, b->new_expr) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const Free *a, const Free *b) {
    return bind(func_rename, a->name, b->name, "Free");
}

bool Matcher::visit(const Realize *a, const Realize *b) {
    if (a->memory_type != b->memory_type) {
        failure_reason = "Realize memory_type mismatch.";
        return false;
    }
    if (a->bounds.size() != b->bounds.size()) {
        failure_reason = "Realize bounds dimension mismatch.";
        return false;
    }
    if (!bind(func_rename, a->name, b->name, "Realize")) {
        return false;
    }
    for (size_t i = 0; i < a->bounds.size(); ++i) {
        if (!match_expr(a->bounds[i].min, b->bounds[i].min) ||
            !match_expr(a->bounds[i].extent, b->bounds[i].extent)) {
            return false;
        }
    }
    return match_expr(a->condition, b->condition) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const Block *a, const Block *b) {
    return match_stmt(a->first, b->first) && match_stmt(a->rest, b->rest);
}

bool Matcher::visit(const Fork *a, const Fork *b) {
    return match_stmt(a->first, b->first) && match_stmt(a->rest, b->rest);
}

bool Matcher::visit(const IfThenElse *a, const IfThenElse *b) {
    return match_expr(a->condition, b->condition) &&
           match_stmt(a->then_case, b->then_case) &&
           match_stmt(a->else_case, b->else_case);
}

bool Matcher::visit(const Evaluate *a, const Evaluate *b) {
    return match_expr(a->value, b->value);
}

bool Matcher::visit(const Prefetch *a, const Prefetch *b) {
    if (!bind(func_rename, a->name, b->name, "Prefetch")) {
        return false;
    }
    if (a->bounds.size() != b->bounds.size()) {
        failure_reason = "Prefetch bounds dimension mismatch.";
        return false;
    }
    for (size_t i = 0; i < a->bounds.size(); ++i) {
        if (!match_expr(a->bounds[i].min, b->bounds[i].min) ||
            !match_expr(a->bounds[i].extent, b->bounds[i].extent)) {
            return false;
        }
    }
    return match_expr(a->condition, b->condition) &&
           match_stmt(a->body, b->body);
}

bool Matcher::visit(const Atomic *a, const Atomic *b) {
    if (!bind(func_rename, a->producer_name, b->producer_name, "Atomic")) {
        return false;
    }
    if (a->mutex_name.empty() != b->mutex_name.empty()) {
        failure_reason = "Atomic mutex presence mismatch.";
        return false;
    }
    if (!a->mutex_name.empty() &&
        !bind(func_rename, a->mutex_name, b->mutex_name, "Atomic mutex")) {
        return false;
    }
    return match_stmt(a->body, b->body);
}

bool Matcher::visit(const HoistedStorage *a, const HoistedStorage *b) {
    return bind(func_rename, a->name, b->name, "HoistedStorage") &&
           match_stmt(a->body, b->body);
}

}  // namespace

MatchResult match_canonical_form(const Stmt &spec_loop,
                                 const Stmt &user_loop) {
    Matcher m;
    MatchResult r;
    r.success = m.match_stmt(spec_loop, user_loop);
    r.failure_reason = std::move(m.failure_reason);
    r.var_rename = std::move(m.var_rename);
    r.func_rename = std::move(m.func_rename);
    return r;
}

}  // namespace Internal
}  // namespace Halide
