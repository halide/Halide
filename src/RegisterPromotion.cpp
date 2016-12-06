#include "RegisterPromotion.h"
#include "Simplify.h"
#include "IRMutator.h"
#include "ExprUsesVar.h"
#include "Solve.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;
using std::set;

namespace {

struct MemoryOp {
    string alloc;
    const Load *load;
    const Store *store;
    bool address_taken;
    bool index_varies;
    bool in_loop;
};

void debug_memory_op(const MemoryOp &op) {
    if (op.store) {
        debug(0) << "  STORE: " << op.alloc << " " << op.store->index << "\n";
    } else if (op.load) {
        debug(0) << "  LOAD: " << op.alloc << " " << op.load->index << "\n";
    } else {
        internal_assert(op.address_taken);
        debug(0) << "  ADDRESS TAKEN:   " << op.alloc << "\n";
    }
}


class FindStrides : public IRVisitor {
    const string &buf;

    using IRVisitor::visit;
    void visit(const Variable *op) {
        if (starts_with(op->name, buf + ".stride.")) {
            result.push(op->name, Interval::everything());
        }
    }
public:
    FindStrides(const string &b) : buf(b) {}
    Scope<Interval> result;
};

enum class AliasResult {Yes, No, Maybe};

AliasResult alias(const MemoryOp &a, const MemoryOp &b) {
    if (a.alloc != b.alloc) {
        return AliasResult::No;
    }

    if (a.address_taken || b.address_taken) {
        return AliasResult::Maybe;
    }

    Expr a_idx = a.load ? a.load->index : a.store->index;
    Expr b_idx = b.load ? b.load->index : b.store->index;

    bool vector_vs_scalar = false;
    if (a_idx.type().is_scalar() && b_idx.type().is_vector()) {
        vector_vs_scalar = true;
        a_idx = Broadcast::make(a_idx, b_idx.type().lanes());
    } else if (b_idx.type().is_scalar() && a_idx.type().is_vector()) {
        vector_vs_scalar = true;
        b_idx = Broadcast::make(b_idx, a_idx.type().lanes());
    }

    if (a_idx.type() != b_idx.type()) {
        // Mismatched vectors
        return AliasResult::Maybe;
    }

    Expr test = simplify(a_idx == b_idx);

    // We really want to do our alias analysis on multi-dimensional
    // coordinates before storage flattening, but that's not possible
    // because we want to happen after loop partitioning,
    // vectorization, and unrolling. We assume that the introduction
    // of strides in the flattening has not created new aliasing, so
    // our test should have a universal quantifier on any strides for
    // the buffer.
    FindStrides strides(a.alloc);
    test.accept(&strides);
    Expr relaxed_test = simplify(and_condition_over_domain(test, strides.result));

    if (is_one(test) && !vector_vs_scalar) {
        return AliasResult::Yes;
    } else if (is_zero(relaxed_test)) {
        return AliasResult::No;
    } else {
        return AliasResult::Maybe;
    }
}

class HasLoadOrImpureCall : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Load *op) {
        // TODO: if the load is from a const buffer, we're actually OK
        result = true;
    }

    void visit(const Call *op) {
        if (op->is_pure()) {
            IRVisitor::visit(op);
        } else {
            result = true;
        }
    }
public:
    bool result = false;
};

bool has_load_or_impure_call(Expr e) {
    HasLoadOrImpureCall h;
    e.accept(&h);
    return h.result;
}

class FindMemoryOps : public IRVisitor {
    using IRVisitor::visit;

    bool in_loop = false;

    void visit(const Load *op) {
        IRVisitor::visit(op);
        bool varies = (expr_uses_vars(op->index, inner_loops) ||
                       has_load_or_impure_call(op->index));
        result.push_back({op->name, op, nullptr, false, varies, in_loop});
    }

    void visit(const Variable *op) {
        if (ends_with(op->name, ".buffer")) {
            result.push_back({op->name.substr(0, op->name.size() - 7),
                        nullptr, nullptr, true, false, in_loop});
        }
    }

    void visit(const Call *op) {
        if (op->is_intrinsic(Call::address_of)) {
            internal_assert(op->args.size() == 1);
            const Load *l = op->args[0].as<Load>();
            internal_assert(l);
            result.push_back({op->name, nullptr, nullptr, true, false, in_loop});
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Store *op) {
        IRVisitor::visit(op);
        bool varies = (expr_uses_vars(op->index, inner_loops) ||
                       has_load_or_impure_call(op->index));
        result.push_back({op->name, nullptr, op, false, varies, in_loop});
    }

    void visit(const For *op) {
        bool old_in_loop = in_loop;
        in_loop = true;
        inner_loops.push(op->name, 0);
        IRVisitor::visit(op);
        in_loop = old_in_loop;
    }

    void visit(const IfThenElse *op) {
        bool old_in_loop = in_loop;
        in_loop = true;
        IRVisitor::visit(op);
        in_loop = old_in_loop;
    }

    void visit(const Let *op) {
        if (expr_uses_vars(op->value, inner_loops)) {
            inner_loops.push(op->name, 0);
        }
        IRVisitor::visit(op);
    }

    void visit(const LetStmt *op) {
        if (expr_uses_vars(op->value, inner_loops)) {
            inner_loops.push(op->name, 0);
        }
        IRVisitor::visit(op);
    }

public:
    vector<MemoryOp> result;
    Scope<int> inner_loops;
};

struct Replacement {
    string name;
    Expr index;
};

class DoReplacements : public IRMutator {
    const map<const IRNode *, Replacement> &replacements;

    using IRMutator::visit;

    void visit(const Load *op) {
        auto it = replacements.find(op);
        if (it != replacements.end()) {
            expr = Load::make(op->type, it->second.name, it->second.index, BufferPtr(), Parameter());
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        auto it = replacements.find(op);
        if (it != replacements.end()) {
            stmt = Store::make(it->second.name, mutate(op->value), it->second.index, Parameter());
        } else {
            IRMutator::visit(op);
        }
    }

public:
    DoReplacements(const map<const IRNode *, Replacement> &r) : replacements(r) {}
};


class RegisterPromotion : public IRMutator {
    using IRMutator::visit;

    void visit(const For *loop) {
        Stmt body = loop->body;

        // Get all memory ops for this allocation
        FindMemoryOps f;
        body.accept(&f);

        debug(0) << "\n\nAt loop over " << loop->name << " memory ops:\n";
        for (const MemoryOp &op : f.result) {
            debug_memory_op(op);
        }

        map<const IRNode *, Replacement> nodes_to_replace;

        // Find subsequences that store with a store, and end with a
        // matching store at the same loop level, where everything in
        // the middle either aliases or doesn't.
        vector<bool> claimed(f.result.size(), false);
        for (size_t i = 0; i < f.result.size(); i++) {

            const MemoryOp &op = f.result[i];
            if (claimed[i] || !op.store || op.index_varies || bad_allocations.count(op.alloc)) {
                continue;
            }
            vector<size_t> subsequence;
            bool any_maybes = false;
            for (size_t j = 0; j < f.result.size(); j++) {
                if (i == j) {
                    subsequence.push_back(i);
                    continue;
                }
                MemoryOp &other_op = f.result[j];
                switch (alias(op, other_op)) {
                case AliasResult::Yes:
                    subsequence.push_back(j);
                    claimed[j] = true;
                    break;
                case AliasResult::No:
                    break;
                case AliasResult::Maybe:
                    any_maybes = true;
                    break;
                }
            }

            if (any_maybes) {
                continue;
            }

            debug(0) << "\nFound subsequence:\n";
            for (size_t j : subsequence) {
                const MemoryOp &op = f.result[j];
                debug_memory_op(op);
            }

            // Make a name for the realization representing the register values
            string tmp_name = unique_name('t');
            Type type;
            Expr heap_index, tmp_index, heap_value, tmp_value;
            string buffer_name;
            Parameter param;
            bool any_loads = false;
            bool first_store_is_unconditional = false;
            for (size_t j : subsequence) {
                const MemoryOp &op = f.result[j];
                const Store *s = op.store;
                const Load *l = op.load;
                if (s && buffer_name.empty()) {
                    first_store_is_unconditional = !op.in_loop;
                    buffer_name = s->name;
                    type = s->value.type();
                    heap_index = s->index;
                    param = s->param;
                    if (type.is_vector()) {
                        tmp_index = Ramp::make(0, 1, type.lanes());
                    } else {
                        tmp_index = 0;
                    }
                    heap_value = Load::make(type, buffer_name, heap_index, BufferPtr(), param);
                    tmp_value = Load::make(type, tmp_name, tmp_index, BufferPtr(), Parameter());
                } else if (l) {
                    any_loads = true;
                }
            }

            if (subsequence.size() == 1 &&
                first_store_is_unconditional) {
                continue;
            }

            Stmt heap_to_tmp = Store::make(tmp_name, heap_value, tmp_index, Parameter());
            Stmt tmp_to_heap = Store::make(buffer_name, tmp_value, heap_index, param);
            if (!any_loads || first_store_is_unconditional) {
                body = Block::make({body, tmp_to_heap});
            } else {
                body = Block::make({heap_to_tmp, body, tmp_to_heap});
            }
            body = Allocate::make(tmp_name, type.element_of(), {type.lanes()}, const_true(), body);
            for (size_t j : subsequence) {
                if (f.result[j].store) {
                    nodes_to_replace[f.result[j].store] = {tmp_name, tmp_index};
                } else {
                    nodes_to_replace[f.result[j].load] = {tmp_name, tmp_index};
                }
            }

            bad_allocations.insert(tmp_name);
        }

        debug(0) << "Performing " << nodes_to_replace.size() << " replacements\n";

        // Do all the replacements
        body = DoReplacements(nodes_to_replace).mutate(body);

        debug(0) << "***********\n" << body << "********\n";

        // Recurse inwards
        body = mutate(body);

        if (body.same_as(loop->body)) {
            stmt = loop;
        } else {
            stmt = For::make(loop->name, loop->min, loop->extent, loop->for_type, loop->device_api, body);
        }
    }

    void visit(const Allocate *op) {
        int32_t sz = op->constant_allocation_size();
        if ((sz > 0 && sz < 1024) ||
            op->new_expr.defined()) {
            bad_allocations.insert(op->name);
        }
        Stmt body = mutate(op->body);
        if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, op->extents, op->condition,
                                  body, op->new_expr, op->free_function);
        }
    }

    set<string> bad_allocations;
};

}

Stmt register_promotion(Stmt s) {
    return RegisterPromotion().mutate(s);
}
}
}
