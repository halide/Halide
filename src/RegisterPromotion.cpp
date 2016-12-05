#include "RegisterPromotion.h"
#include "Simplify.h"
#include "IRMutator.h"
#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

namespace {

struct MemoryOp {
    std::string alloc;
    const Load *load;
    const Store *store;
    bool address_taken;
    bool index_varies;
    bool in_loop;
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
    debug(0) << test << "\n";
    if (is_one(test) && !vector_vs_scalar) {
        return AliasResult::Yes;
    } else if (is_zero(test)) {
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
    std::vector<MemoryOp> result;
    Scope<int> inner_loops;
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
            if (op.store) {
                debug(0) << "  STORE: " << op.alloc << "   " << op.store->index << "\n";                
            } else if (op.load) {
                debug(0) << "  LOAD:  " << op.alloc << "   " << op.load->index << "\n";
            } else if (op.load) {
                debug(0) << "  ADDRESS TAKEN:   " << op.alloc << "\n";
            }
        }
        
        // Find subsequences that store with a store, and end with a
        // matching store at the same loop level, where everything in
        // the middle either aliases or doesn't.
        std::vector<bool> claimed(f.result.size(), false);
        for (size_t i = 0; i < f.result.size(); i++) {
            const MemoryOp &op = f.result[i];
            if (claimed[i] || !op.store || op.index_varies || bad_allocations.count(op.alloc)) {
                continue;
            }
            std::vector<size_t> subsequence;
            subsequence.push_back(i);
            bool done = false;
            for (size_t j = i+1; j < f.result.size() && !done; j++) {
                MemoryOp &other_op = f.result[j];
                debug(0) << "Checking " << i << " vs " << j << "\n";
                switch (alias(op, other_op)) {
                case AliasResult::Yes:
                    debug(0) << "Yes\n";
                    subsequence.push_back(j);
                    break;
                case AliasResult::No:
                    debug(0) << "No\n";
                    break;
                case AliasResult::Maybe:
                    debug(0) << "Maybe\n";
                    done = true;
                    break;
                }               
            }            

            debug(0) << "Subsequence of length " << subsequence.size() << "\n";
            while (!subsequence.empty() &&
                   (!f.result[subsequence.back()].store ||
                    f.result[subsequence.back()].in_loop)) {
                subsequence.pop_back();                
            }

            if (subsequence.size() <= 1) {
                continue;
            }

            debug(0) << "\nFound subsequence:\n";
            for (size_t j : subsequence) {
                const MemoryOp &op = f.result[j];
                claimed[j] = true;
                if (op.store) {
                    debug(0) << "  STORE: " << op.alloc << "   " << op.store->index << "\n";                
                } else if (op.load) {
                    debug(0) << "  LOAD:  " << op.alloc << "   " << op.load->index << "\n";
                } else if (op.load) {
                    debug(0) << "  ADDRESS TAKEN:   " << op.alloc << "\n";
                }
            }

            // Don't start future subsequences at any of these stores
        }
        
        body = mutate(body);
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
                                  op->body, op->new_expr, op->free_function);
        }
    }

    std::set<std::string> bad_allocations;
};

}

Stmt register_promotion(Stmt s) {
    return RegisterPromotion().mutate(s);
}
}
}
