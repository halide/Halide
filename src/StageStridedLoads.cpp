#include "StageStridedLoads.h"
#include "CSE.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"

namespace Halide {
namespace Internal {

namespace {

class FindStridedLoads : public IRVisitor {
public:
    struct Key {
        // The buffer being accessed.
        std::string buf;
        // The base index being accessed, without any constant offset.
        Expr base;

        // The stride and lanes of the vector access.
        int64_t stride;
        int lanes;

        // The loaded type.
        Type type;

        // The Allocate node the load belongs to. nullptr for loads from external buffers.
        const Allocate *allocation;

        // The Stmt over which the load definitely happens, and definitely
        // refers to the same buffer as other loads with the same name. nullptr
        // means global scope.
        const IRNode *scope;

        bool operator<(const Key &other) const {
            // Check fields in order of cost to compare
            if (stride < other.stride) {
                return true;
            } else if (stride > other.stride) {
                return false;
            } else if (lanes < other.lanes) {
                return true;
            } else if (lanes > other.lanes) {
                return false;
            } else if (scope < other.scope) {
                return true;
            } else if (scope > other.scope) {
                return false;
            } else if (allocation < other.allocation) {
                return true;
            } else if (allocation > other.allocation) {
                return false;
            } else if (type < other.type) {
                return true;
            } else if (other.type < type) {
                return false;
            } else if (buf < other.buf) {
                return true;
            } else if (buf > other.buf) {
                return false;
            } else {
                return graph_less_than(base, other.base);
            }
        }
    };
    // Entry entry maps from an offset from the base to a vector of identical
    // Load nodes with that offset.
    std::map<Key, std::map<int64_t, std::vector<const Load *>>> found_loads;

    // The current scope over which accesses definitely occur.
    const IRNode *scope = nullptr;

    Scope<const Allocate *> allocation_scope;

    std::map<const IRNode *, const IRNode *> parent_scope;

protected:
    void visit(const Load *op) override {
        if (is_const_one(op->predicate)) {
            Expr idx = substitute_in_all_lets(simplify(common_subexpression_elimination(op->index)));
            if (const Ramp *r = idx.as<Ramp>()) {
                const int64_t *stride_ptr = as_const_int(r->stride);
                int64_t stride = stride_ptr ? *stride_ptr : 0;
                Expr base = r->base;
                int64_t offset = 0;
                const Add *base_add = base.as<Add>();
                const int64_t *offset_ptr = base_add ? as_const_int(base_add->b) : nullptr;
                if (offset_ptr) {
                    base = base_add->a;
                    offset = *offset_ptr;
                }
                // TODO: handle nested ramps here?
                if (stride >= 2 && stride < r->lanes && r->stride.type().is_scalar()) {
                    const IRNode *s = scope;
                    const Allocate *a = nullptr;
                    if (allocation_scope.contains(op->name)) {
                        a = allocation_scope.get(op->name);
                    }
                    found_loads[Key{op->name, base, stride, r->lanes, op->type, a, s}][offset].push_back(op);
                }
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const For *op) override {
        if (can_prove(op->extent > 0)) {
            // The loop body definitely runs

            // TODO: worry about different iterations of the loop somehow not
            // providing the evidence we thought it did.
            IRVisitor::visit(op);
        } else {
            const IRNode *child_scope = op->body.get();
            parent_scope[child_scope] = scope;
            ScopedValue<const IRNode *> bind(scope, child_scope);
            IRVisitor::visit(op);
        }
    }

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);
        {
            const IRNode *child_scope = op->then_case.get();
            parent_scope[child_scope] = scope;
            ScopedValue<const IRNode *> bind(scope, child_scope);
            op->then_case.accept(this);
        }
        if (op->else_case.defined()) {
            const IRNode *child_scope = op->else_case.get();
            parent_scope[child_scope] = scope;
            ScopedValue<const IRNode *> bind(scope, child_scope);
            op->else_case.accept(this);
        }
    }

    void visit(const Allocate *op) override {
        // Provide a mapping from load nodes to paddable allocations they belong
        // to.
        ScopedBinding<const Allocate *> bind(allocation_scope, op->name, op);
        IRVisitor::visit(op);
    }

    using IRVisitor::visit;
};

// Replace a bunch of load expressions in a stmt
class ReplaceStridedLoads : public IRMutator {
public:
    std::map<const Load *, Expr> replacements;
    std::map<const Allocate *, int> padding;

protected:
    Expr visit(const Load *op) override {
        auto it = replacements.find(op);
        if (it != replacements.end()) {
            return mutate(it->second);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        auto it = padding.find(op);
        Stmt s = IRMutator::visit(op);
        if (it == padding.end()) {
            return s;
        } else {
            op = s.as<Allocate>();
            internal_assert(op);
            return Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, op->condition,
                                  op->body, op->new_expr, op->free_function,
                                  std::max(it->second, op->padding));
        }
    }

    using IRMutator::visit;
};

}  // namespace

Stmt stage_strided_loads(const Stmt &s) {
    Stmt stmt = s;
    // The following pass assumes that same variable name is the same variable,
    // which is violated by the prior unrolling pass. Fix it up first.
    stmt = uniquify_variable_names(stmt);

    FindStridedLoads finder;
    ReplaceStridedLoads replacer;

    // Find related clusters of strided loads anywhere in the stmt. While this
    // appears to look globally, it requires expressions to match exactly, so
    // really it's only going to find things inside the same loops and let
    // statements.
    s.accept(&finder);

    for (const auto &l : finder.found_loads) {
        const FindStridedLoads::Key &k = l.first;
        const std::map<int64_t, std::vector<const Load *>> &v = l.second;

        // TODO: cluster the loads looking for groups with *relative* offsets between 0 and stride-1
        // Consider f(2*x) + f(2*x + 1) + f(2*x + 2). What loads do we want?
        for (auto load = v.begin(); load != v.end();) {
            // If there is any other load at the same base at an offset at least
            // stride-1 ahead, it's safe to do a big dense load. Note that we're
            // assuming that it's always valid to load addresses between two
            // valid addresses, which rules out games involving protected pages
            // at the end of scanlines.
            const bool can_lift = l.second.lower_bound(load->first + k.stride - 1) != l.second.end();

            if (!can_lift) {
                load++;
                continue;
            }

            // We have a complete cluster of loads. Make a single dense load
            int lanes = k.lanes * k.stride;
            int64_t first_offset = load->first;
            Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = load->second[0];
            Expr shared_load = Load::make(t, k.buf, idx, op->image, op->param,
                                          const_true(lanes), op->alignment);
            shared_load = common_subexpression_elimination(shared_load);
            for (; load != v.end() && load->first < first_offset + k.stride; load++) {
                Expr shuf = Shuffle::make_slice(shared_load, load->first - first_offset, k.stride, k.lanes);
                for (const Load *l : load->second) {
                    replacer.replacements.emplace(l, shuf);
                }
            }
        }

        // Do the same in reverse to pick up any loads that didn't get
        // picked up in a cluster, but for whom we know it's safe to do a
        // dense load before their start.
        for (auto load = v.rbegin(); load != v.rend(); load++) {
            if (replacer.replacements.count(load->second[0])) {
                continue;
            }
            int64_t delta = k.stride - 1;
            const bool can_lift = l.second.upper_bound(load->first - delta) != l.second.begin();
            if (!can_lift) {
                continue;
            }
            int lanes = k.lanes * k.stride;
            int64_t first_offset = load->first - delta;
            Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = load->second[0];
            Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                         const_true(lanes), op->alignment - delta);
            dense_load = common_subexpression_elimination(dense_load);
            Expr shuf = Shuffle::make_slice(dense_load, delta, k.stride, k.lanes);
            for (const Load *l : load->second) {
                replacer.replacements.emplace(l, shuf);
            }
        }

        // Look for any loads we can densify because an overlapping load occurs
        // in any parent scope.
        for (auto load = v.rbegin(); load != v.rend(); load++) {
            if (replacer.replacements.count(load->second[0])) {
                continue;
            }
            int64_t min_offset = load->first;
            int64_t max_offset = load->first;
            const IRNode *scope = k.scope;
            while (scope) {
                const IRNode *parent = finder.parent_scope[scope];
                auto parent_key = k;
                parent_key.scope = parent;
                auto it = finder.found_loads.find(parent_key);
                if (it != finder.found_loads.end() && !it->second.empty()) {
                    min_offset = std::min(it->second.begin()->first, min_offset);
                    max_offset = std::max(it->second.rbegin()->first, max_offset);
                }
                scope = parent;
            }

            if (max_offset - min_offset < k.stride - 1) {
                continue;
            }
            int64_t offset = std::max(load->first - (k.stride - 1), min_offset);
            int lanes = k.lanes * k.stride;
            Expr idx = Ramp::make(k.base + (int)offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = load->second[0];
            Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                         const_true(lanes), op->alignment);
            dense_load = common_subexpression_elimination(dense_load);
            Expr shuf = Shuffle::make_slice(dense_load, load->first - offset, k.stride, k.lanes);
            for (const Load *l : load->second) {
                replacer.replacements.emplace(l, shuf);
            }
        }

        // Densify any remaining strided loads to internal allocations by
        // padding the allocation, and densify any remaining strided loads to
        // external allocations by doing a dense load at a trimmed size. We rely
        // on codegen to do a good job at loading vectors of a funny size.
        for (auto load = v.begin(); load != v.end(); load++) {  // NOLINT
            if (replacer.replacements.count(load->second[0])) {
                continue;
            }

            int lanes = k.lanes * k.stride;

            bool may_pad = k.allocation && !k.allocation->new_expr.defined();
            if (may_pad) {
                int padding = (int)(k.stride - 1);
                auto p = replacer.padding.insert({k.allocation, padding});
                if (!p.second) {
                    p.first->second = std::max(p.first->second, padding);
                }
            } else {
                lanes -= k.stride - 1;
            }

            int64_t first_offset = load->first;
            Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = load->second[0];
            Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                         const_true(lanes), op->alignment);
            dense_load = common_subexpression_elimination(dense_load);
            Expr shuf = Shuffle::make_slice(dense_load, load->first - first_offset, k.stride, k.lanes);
            for (const Load *l : load->second) {
                replacer.replacements.emplace(l, shuf);
            }
        }
    }

    // TODO: What about sharing a conditional solo with an unconditional pair

    return replacer.mutate(stmt);
}

}  // namespace Internal
}  // namespace Halide
