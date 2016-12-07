#include "FragmentAllocations.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

namespace {

using std::vector;

// A structure to represent a constant index to one of the buffers
// we're trying to bust up into scalar allocations. We only handle two
// types of constant index - scalar ints and ramps where the base and
// stride are scalar ints.
struct UnpackedIndex {
    // If the index was scalar and a constant, this is the value.
    const int64_t *index;
    // If this index was a constant ramp, these are populated instead.
    const int64_t *base, *stride;

    UnpackedIndex(Expr e) {
        index = as_const_int(e);
        const Ramp *ramp = e.as<Ramp>();
        base = ramp ? as_const_int(ramp->base) : nullptr;
        stride = ramp ? as_const_int(ramp->stride) : nullptr;
    }

    bool is_constant() const {
        return index || (base && stride);
    }

    // Get the value in a given lane
    int64_t value(int lane = 0) const {
        if (index) {
            return *index;
        } else {
            return (*base) + lane * (*stride);
        }
    }
};

class TryFragmentSingleAllocation : public IRMutator {
    using IRVisitor::visit;
    const std::string &name;

    void visit(const Load *op) {
        if (op->name == name) {
            UnpackedIndex idx(op->index);
            if (!idx.is_constant()) {
                success = false;
                expr = op;
            } else if (op->type.is_scalar()) {
                expr = Load::make(op->type, op->name + "." + std::to_string(idx.value()),
                                  0, op->image, op->param);
            } else {
                vector<Expr> lanes;
                for (int i = 0; i < op->type.lanes(); i++) {
                    Expr load = Load::make(op->type.element_of(),
                                           op->name + "." + std::to_string(idx.value(i)),
                                           0, op->image, op->param);
                    lanes.push_back(load);
                }
                expr = Call::make(op->type, Call::concat_vectors, lanes, Call::PureIntrinsic);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        if (op->name == name) {
            UnpackedIndex idx(op->index);
            if (!idx.is_constant()) {
                success = false;
                stmt = op;
            } else if (op->value.type().is_scalar()) {
                stmt = Store::make(op->name + "." + std::to_string(idx.value()),
                                   mutate(op->value), 0, op->param);
            } else {
                vector<Stmt> stores;
                std::string var_name = unique_name('t');
                Expr value_var = Variable::make(op->value.type(), var_name);
                for (int i = 0; i < op->value.type().lanes(); i++) {
                    Expr val = Call::make(op->value.type().element_of(),
                                          Call::shuffle_vector, {value_var, i}, Call::PureIntrinsic);
                    Stmt store = Store::make(op->name + "." + std::to_string(idx.value(i)),
                                             val, 0, op->param);
                    stores.push_back(store);
                }
                stmt = Block::make(stores);
                stmt = LetStmt::make(var_name, mutate(op->value), stmt);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Free *op) {
        if (op->name == name) {
            // No need to free scalar allocations
            stmt = Evaluate::make(0);
        } else {
            IRVisitor::visit(op);
        }
    }

public:

    TryFragmentSingleAllocation(const std::string &n) : name(n) {}
    bool success = true;
};

class FragmentAllocations : public IRMutator {
    using IRMutator::visit;

    void visit(const Allocate *op) {
        size_t sz = op->constant_allocation_size();
        if (op->extents.empty() || sz == 0) {
            IRMutator::visit(op);
        }

        Stmt body = mutate(op->body);

        TryFragmentSingleAllocation fragmenter(op->name);
        Stmt fragmented_body = fragmenter.mutate(body);
        if (fragmenter.success) {
            for (size_t i = sz; i > 0; i--) {
                fragmented_body = Allocate::make(op->name + "." + std::to_string(i-1),
                                              op->type, {}, const_true(), fragmented_body);
            }
            stmt = fragmented_body;
        } else {
            stmt = Allocate::make(op->name, op->type, op->extents, const_true(), body);
        }
    }
};

}

Stmt fragment_allocations(Stmt s) {
    return FragmentAllocations().mutate(s);
}

class FragmentCUDALocalAllocations : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            stmt = fragment_allocations(Stmt(op));
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt fragment_cuda_local_allocations(Stmt s) {
    return FragmentCUDALocalAllocations().mutate(s);
}

}
}
