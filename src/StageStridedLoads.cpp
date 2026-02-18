#include "StageStridedLoads.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

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
            // We want to give ourselves the best possible chance at recognizing
            // a naked Ramp, so we simplify and substitute in lets (and take
            // care to treat the index expression as a graph until the next
            // CSE).
            Expr idx = substitute_in_all_lets(simplify(common_subexpression_elimination(op->index)));
            if (const Ramp *r = idx.as<Ramp>()) {
                int64_t stride = as_const_int(r->stride).value_or(0);
                Expr base = r->base;
                int64_t offset = 0;
                if (const Add *base_add = base.as<Add>()) {
                    if (auto off = as_const_int(base_add->b)) {
                        base = base_add->a;
                        offset = *off;
                    }
                } else if (auto off = as_const_int(base)) {
                    base = 0;
                    offset = *off;
                }

                // TODO: We do not yet handle nested vectorization here for
                // ramps which have not already collapsed. We could potentially
                // handle more interesting types of shuffle than simple flat slices.
                if (stride >= 2 && stride <= r->lanes && r->stride.type().is_scalar()) {
                    const IRNode *s = scope;
                    const Allocate *a = nullptr;
                    if (const Allocate *const *a_ptr = allocation_scope.find(op->name)) {
                        a = *a_ptr;
                    }
                    found_loads[Key{op->name, base, stride, r->lanes, op->type, a, s}][offset].push_back(op);
                }
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const For *op) override {
        if (can_prove(op->min <= op->max)) {
            // The loop body definitely runs
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
    std::map<std::pair<const Allocate *, const Load *>, Expr> replacements;
    std::map<const Allocate *, int> padding;
    Scope<const Allocate *> allocation_scope;
    std::map<Stmt, std::pair<std::string, Expr>> let_injections;

    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        auto it = let_injections.find(s);
        if (it != let_injections.end()) {
            const auto &[name, value] = it->second;
            return LetStmt::make(name, value, IRMutator::mutate(s));
        } else {
            return IRMutator::mutate(s);
        }
    }

protected:
    Expr visit(const Load *op) override {
        const Allocate *alloc = nullptr;
        if (const Allocate *const *a_ptr = allocation_scope.find(op->name)) {
            alloc = *a_ptr;
        }
        auto it = replacements.find({alloc, op});
        if (it != replacements.end()) {
            return mutate(it->second);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        ScopedBinding bind(allocation_scope, op->name, op);
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

Stmt innermost_containing_stmt(const Stmt &root, const std::set<const Load *> &exprs) {
    std::vector<Stmt> path, result;
    mutate_with(root,  //
                [&](auto *self, const Stmt &s) {
                    path.push_back(s);
                    self->mutate_base(s);
                    path.pop_back();
                    return s;  //
                },
                [&](auto *self, const Expr &e) {
                    const Load *l = e.as<Load>();
                    if (l && exprs.count(l)) {
                        if (result.empty()) {
                            result = path;
                        } else {
                            // Find the common prefix of path and result
                            size_t i = 0;
                            while (i < path.size() &&
                                   i < result.size() &&
                                   path[i].get() == result[i].get()) {
                                i++;
                            }
                            result.resize(i);
                        }
                    };
                    return self->mutate_base(e);  //
                });
    internal_assert(!result.empty()) << "None of the exprs were found\n";
    return result.back();
}

bool can_hoist_shared_load(const Stmt &s, const std::string &buf, const Expr &idx) {
    // Check none of the variables the idx depends on are defined somewhere
    // within this stmt, and there are no stores to the given buffer in the
    // stmt.
    bool result = true;
    visit_with(s,                                 //
               [&](auto *self, const Let *let) {  //
                   result &= !expr_uses_var(idx, let->name);
               },
               [&](auto *self, const LetStmt *let) {  //
                   result &= !expr_uses_var(idx, let->name);
               },
               [&](auto *self, const For *loop) {  //
                   result &= !expr_uses_var(idx, loop->name);
               },
               [&](auto *self, const Allocate *alloc) {  //
                   result &= alloc->name != buf;
               },
               [&](auto *self, const Store *store) {  //
                   result &= store->name != buf;
               });
    return result;
}

}  // namespace

Stmt stage_strided_loads(const Stmt &s) {
    FindStridedLoads finder;
    ReplaceStridedLoads replacer;

    // Find related clusters of strided loads anywhere in the stmt. While this
    // appears to look globally, it requires expressions to match exactly, so
    // really it's only going to find things inside the same loops and let
    // statements.
    s.accept(&finder);

    for (const auto &l : finder.found_loads) {
        const FindStridedLoads::Key &k = l.first;
        const Allocate *alloc = k.allocation;
        const std::map<int64_t, std::vector<const Load *>> &v = l.second;

        // Find clusters of strided loads that can share the same dense load.
        for (auto load = v.begin(); load != v.end();) {
            // If there is any other load at the same base at an offset at least
            // stride-1 ahead, it's safe to do a big dense load. Note that we're
            // assuming that it's always valid to load addresses between two
            // valid addresses, which rules out games involving protected pages
            // at the end of scanlines.
            const bool can_lift = l.second.lower_bound(load->first + k.stride - 1) != l.second.end();

            if (!can_lift) {
                debug(0) << "Can't lift: " << Expr(load->second[0]->index) << "\n";
                load++;
                continue;
            }

            // We have a complete cluster of loads. Make a single dense load
            int lanes = k.lanes * k.stride;
            int64_t first_offset = load->first;
            Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = load->second[0];

            std::set<const Load *> all_loads;
            for (auto l = load; l != v.end() && l->first < first_offset + k.stride; l++) {
                all_loads.insert(l->second.begin(), l->second.end());
            }

            Expr shared_load = Load::make(t, k.buf, idx, op->image, op->param,
                                          const_true(lanes), op->alignment);
            shared_load = common_subexpression_elimination(shared_load);

            // If possible, we do the shuffle as an in-place transpose followed
            // by a dense slice. This is more efficient when extracting multiple
            // slices.
            Stmt let_site = innermost_containing_stmt(alloc ? Stmt(alloc) : s, all_loads);
            if (can_hoist_shared_load(let_site, k.buf, idx)) {
                shared_load = Shuffle::make_transpose(shared_load, k.stride);
                std::string name = unique_name('t');
                Expr var = Variable::make(shared_load.type(), name);
                for (; load != v.end() && load->first < first_offset + k.stride; load++) {
                    int row = load->first - first_offset;
                    Expr shuf = Shuffle::make_slice(var, row * k.lanes, 1, k.lanes);
                    for (const Load *l : load->second) {
                        replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
                    }
                }
                replacer.let_injections.emplace(let_site, std::make_pair(name, shared_load));
            } else {
                for (; load != v.end() && load->first < first_offset + k.stride; load++) {
                    int row = load->first - first_offset;
                    Expr shuf = Shuffle::make_slice(shared_load, row, k.stride, k.lanes);
                    for (const Load *l : load->second) {
                        replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
                    }
                }
            }
        }

        // Do the same in reverse to pick up any loads that didn't get
        // picked up in a cluster, but for whom we know it's safe to do a
        // dense load before their start.
        for (const auto &[offset, loads] : reverse_view(v)) {
            if (replacer.replacements.count({alloc, loads[0]})) {
                continue;
            }
            int64_t delta = k.stride - 1;
            const bool can_lift = l.second.upper_bound(offset - delta) != l.second.begin();
            if (!can_lift) {
                continue;
            }
            int lanes = k.lanes * k.stride;
            int64_t first_offset = offset - delta;
            Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = loads[0];
            Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                         const_true(lanes), op->alignment - delta);
            dense_load = common_subexpression_elimination(dense_load);
            Expr shuf = Shuffle::make_slice(dense_load, delta, k.stride, k.lanes);
            for (const Load *l : loads) {
                replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
            }
        }

        // Look for any loads we can densify because an overlapping load occurs
        // in any parent scope.
        for (const auto &[offset, loads] : reverse_view(v)) {
            if (replacer.replacements.count({alloc, loads[0]})) {
                continue;
            }
            int64_t min_offset = offset;
            int64_t max_offset = offset;
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
            int64_t final_offset = std::max(offset - (k.stride - 1), min_offset);
            int lanes = k.lanes * k.stride;
            Expr idx = Ramp::make(k.base + (int)final_offset, make_one(k.base.type()), lanes);
            Type t = k.type.with_lanes(lanes);
            const Load *op = loads[0];
            Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                         const_true(lanes), op->alignment);
            dense_load = common_subexpression_elimination(dense_load);
            Expr shuf = Shuffle::make_slice(dense_load, offset - final_offset, k.stride, k.lanes);
            for (const Load *l : loads) {
                replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
            }
        }

        // Densify any remaining strided loads to internal allocations by
        // padding the allocation, and densify any remaining strided loads to
        // external allocations by doing a dense load at a trimmed size. We rely
        // on codegen to do a good job at loading vectors of a funny size.
        for (const auto &[offset, loads] : v) {
            if (replacer.replacements.count({alloc, loads[0]})) {
                continue;
            }

            int lanes = k.lanes * k.stride;

            bool may_pad = k.allocation && !k.allocation->new_expr.defined();
            int delta = (int)(k.stride - 1);

            if (may_pad) {
                auto p = replacer.padding.insert({k.allocation, delta});
                if (!p.second) {
                    p.first->second = std::max(p.first->second, delta);
                }

                int64_t first_offset = offset;
                Expr idx = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), lanes);
                Type t = k.type.with_lanes(lanes);
                const Load *op = loads[0];
                Expr dense_load = Load::make(t, k.buf, idx, op->image, op->param,
                                             const_true(lanes), op->alignment);
                dense_load = common_subexpression_elimination(dense_load);
                Expr shuf = Shuffle::make_slice(dense_load, offset - first_offset, k.stride, k.lanes);
                for (const Load *l : loads) {
                    replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
                }

            } else if (k.lanes % 2 == 0) {
                // Do two overlapping half-sized dense loads and mush them together.
                int64_t first_offset = offset;
                int half_lanes = lanes / 2;
                internal_assert(delta <= half_lanes);
                Expr idx1 = Ramp::make(k.base + (int)first_offset, make_one(k.base.type()), half_lanes);

                Expr idx2 = Ramp::make(k.base + (int)first_offset + half_lanes - delta, make_one(k.base.type()), half_lanes);
                Type t = k.type.with_lanes(half_lanes);
                const Load *op = loads[0];
                Expr dense_load1 = Load::make(t, k.buf, idx1, op->image, op->param,
                                              const_true(half_lanes), op->alignment);
                Expr dense_load2 = Load::make(t, k.buf, idx2, op->image, op->param,
                                              const_true(half_lanes), op->alignment + half_lanes - delta);
                dense_load1 = common_subexpression_elimination(dense_load1);
                dense_load2 = common_subexpression_elimination(dense_load2);
                Expr shuf1 = Shuffle::make_slice(dense_load1, 0, k.stride, k.lanes / 2);
                Expr shuf2 = Shuffle::make_slice(dense_load2, delta, k.stride, k.lanes / 2);
                Expr shuf = Shuffle::make_concat({shuf1, shuf2});
                for (const Load *l : loads) {
                    replacer.replacements.emplace(std::make_pair(alloc, l), shuf);
                }
            }
        }
    }

    return replacer.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
