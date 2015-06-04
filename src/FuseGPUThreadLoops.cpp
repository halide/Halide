#include "FuseGPUThreadLoops.h"
#include "CodeGen_GPU_Dev.h"
#include "IR.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Bounds.h"
#include "Substitute.h"
#include "IREquality.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::set;

namespace {
string thread_names[] = {"__thread_id_x", "__thread_id_y", "__thread_id_z", "__thread_id_w"};
string shared_mem_name = "__shared";
}

class InjectThreadBarriers : public IRMutator {
    bool in_threads;

    using IRMutator::visit;

    Stmt barrier;

    void visit(const For *op) {
        bool old_in_threads = in_threads;
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            in_threads = true;
        }

        IRMutator::visit(op);

        in_threads = old_in_threads;
    }

    void visit(const ProducerConsumer *op) {
        if (!in_threads) {
            Stmt produce = mutate(op->produce);
            if (!is_no_op(produce)) {
                produce = Block::make(produce, barrier);
            }

            Stmt update;
            if (op->update.defined()) {
                update = mutate(op->update);
                if (!is_no_op(update)) {
                    update = Block::make(update, barrier);
                }
            }

            Stmt consume = mutate(op->consume);

            stmt = ProducerConsumer::make(op->name, produce, update, consume);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Block *op) {
        if (!in_threads && op->rest.defined()) {
            Stmt first = mutate(op->first);
            Stmt rest = mutate(op->rest);
            stmt = Block::make(Block::make(first, barrier), rest);
        } else {
            IRMutator::visit(op);
        }
    }

public:
    InjectThreadBarriers() : in_threads(false) {
        barrier =
            Evaluate::make(Call::make(Int(32), "halide_gpu_thread_barrier",
                                      vector<Expr>(), Call::Extern));
    }
};


class ExtractBlockSize : public IRVisitor {
    Expr block_extent[4];

    Scope<Interval> scope;

    using IRVisitor::visit;

    void found_for(int dim, Expr extent) {
        internal_assert(dim >= 0 && dim < 4);
        if (!block_extent[dim].defined()) {
            block_extent[dim] = extent;
        } else {
            block_extent[dim] = simplify(Max::make(extent, block_extent[dim]));
        }
    }

    void visit(const For *op) {
        Interval ie = bounds_of_expr_in_scope(op->extent, scope);
        Interval im = bounds_of_expr_in_scope(op->min, scope);

        for (int i = 0; i < 4; i++) {
            if (ends_with(op->name, thread_names[i])) {
                found_for(i, ie.max);
            }
        }

        scope.push(op->name, Interval(im.min, im.max + ie.max - 1));
        IRVisitor::visit(op);
        scope.pop(op->name);
    }

    void visit(const LetStmt *op) {
        Interval i = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, i);
        op->body.accept(this);
        scope.pop(op->name);
    }

public:
    int dimensions() const {
        for (int i = 0; i < 4; i++) {
            if (!block_extent[i].defined()) {
                return i;
            }
        }
        return 4;
    }

    Expr extent(int d) const {
        return block_extent[d];
    }

    void max_over_blocks(const Scope<Interval> &scope) {
        for (int i = 0; i < 4; i++) {
            if (block_extent[i].defined()) {
                block_extent[i] = simplify(block_extent[i]);
                block_extent[i] = bounds_of_expr_in_scope(block_extent[i], scope).max;
            }
        }
    }

};

class NormalizeDimensionality : public IRMutator {
    using IRMutator::visit;

    const ExtractBlockSize &block_size;
    const DeviceAPI device_api;

    int depth;
    int max_depth;

    Stmt wrap(Stmt s) {
        if (depth != 0) {
            return mutate(s);
        }
        max_depth = 0;
        s = mutate(s);
        if (is_no_op(s)) {
            return s;
        }
        while (max_depth < block_size.dimensions()) {
            string name = thread_names[max_depth];
            s = For::make("." + name, 0, 1, ForType::Parallel, device_api, s);
            max_depth++;
        }
        return s;
    }

    void visit(const ProducerConsumer *op) {
        Stmt produce = wrap(op->produce);
        Stmt update;
        if (op->update.defined()) {
            update = wrap(op->update);
        }
        Stmt consume = wrap(op->consume);

        if (produce.same_as(op->produce) &&
            update.same_as(op->update) &&
            consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = ProducerConsumer::make(op->name, produce, update, consume);
        }
    }

    void visit(const Block *op) {
        Stmt first = wrap(op->first);

        Stmt rest;
        if (op->rest.defined()) {
            rest = wrap(op->rest);
        }

        if (first.same_as(op->first) &&
            rest.same_as(op->rest)) {
            stmt = op;
        } else {
            stmt = Block::make(first, rest);
        }
    }

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            depth++;
            if (depth > max_depth) {
                max_depth = depth;
            }
            IRMutator::visit(op);
            depth--;
        } else {
            IRMutator::visit(op);
        }
    }

public:
    NormalizeDimensionality(const ExtractBlockSize &e, DeviceAPI device_api)
      : block_size(e), device_api(device_api), depth(0), max_depth(0) {}
};

class ReplaceForWithIf : public IRMutator {
    using IRMutator::visit;

    const ExtractBlockSize &block_size;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            int dim;
            for (dim = 0; dim < 4; dim++) {
                if (ends_with(op->name, thread_names[dim])) {
                    break;
                }
            }

            internal_assert(dim >= 0 && dim < block_size.dimensions());

            Expr var = Variable::make(Int(32), "." + thread_names[dim]);
            internal_assert(is_zero(op->min));
            Stmt body = mutate(op->body);

            body = substitute(op->name, var, body);

            if (equal(op->extent, block_size.extent(dim))) {
                stmt = body;
            } else {
                Expr cond = var < op->extent;
                stmt = IfThenElse::make(cond, body, Stmt());
            }
        } else {
            IRMutator::visit(op);
        }
    }

public:
    ReplaceForWithIf(const ExtractBlockSize &e) : block_size(e) {}
};

class ExtractSharedAllocations : public IRMutator {
    using IRMutator::visit;

    struct SharedAllocation {
        string name;
        Type type;
        Expr size;
    };
    vector<SharedAllocation> allocations;

    Scope<Interval> scope;

    set<string> shared;

    bool in_threads;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_thread_var(op->name)) {
            bool old = in_threads;
            in_threads = true;
            IRMutator::visit(op);
            in_threads = old;
        } else {
            Interval min_bounds = bounds_of_expr_in_scope(op->min, scope);
            Interval extent_bounds = bounds_of_expr_in_scope(op->extent, scope);
            Interval bounds(min_bounds.min, min_bounds.max + extent_bounds.max - 1);
            scope.push(op->name, bounds);
            IRMutator::visit(op);
            scope.pop(op->name);
        }
    }

    void visit(const Allocate *op) {
        if (in_threads) {
            IRMutator::visit(op);
            return;
        }

        shared.insert(op->name);
        IRMutator::visit(op);
        shared.erase(op->name);
        op = stmt.as<Allocate>();
        internal_assert(op);

        SharedAllocation alloc;
        alloc.name = op->name;
        alloc.type = op->type;
        alloc.size = 1;
        for (size_t i = 0; i < op->extents.size(); i++) {
            alloc.size *= op->extents[i];
        }
        alloc.size = bounds_of_expr_in_scope(simplify(alloc.size), scope).max;
        allocations.push_back(alloc);
        stmt = op->body;

    }

    void visit(const Load *op) {
        if (shared.count(op->name)) {
            Expr base = Variable::make(Int(32), op->name + ".shared_offset");
            Expr index = mutate(op->index);
            expr = Load::make(op->type, shared_mem_name, base + index, op->image, op->param);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        if (shared.count(op->name)) {
            Expr base = Variable::make(Int(32), op->name + ".shared_offset");
            Expr index = mutate(op->index);
            Expr value = mutate(op->value);
            stmt = Store::make(shared_mem_name, value, base + index);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (in_threads) {
            IRMutator::visit(op);
            return;
        }

        Expr value = mutate(op->value);
        Interval bounds = bounds_of_expr_in_scope(value, scope);
        bounds.min = simplify(bounds.min);
        bounds.max = simplify(bounds.max);
        scope.push(op->name, bounds);
        Stmt body = mutate(op->body);
        scope.pop(op->name);

        if (op->body.same_as(body) && op->value.same_as(value)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }

public:
    Stmt rewrap(Stmt s) {
        // Sort the allocations by size in bytes of the primitive
        // type. Because the type sizes are then decreasing powers of
        // two, doing this guarantees that all allocations are aligned
        // to then element type as long as the original one is aligned
        // to the widest type.
        for (size_t i = 1; i < allocations.size(); i++) {
            for (size_t j = i; j > 0; j--) {
                if (allocations[j].type.bytes() > allocations[j - 1].type.bytes()) {
                    std::swap(allocations[j], allocations[j - 1]);
                }
            }
        }

        // Add a dummy allocation at the end to get the total size
        SharedAllocation sentinel;
        sentinel.name = "sentinel";
        sentinel.type = UInt(8);
        sentinel.size = 0;
        allocations.push_back(sentinel);

        Expr total_size = Variable::make(Int(32), allocations.back().name + ".shared_offset");
        s = Allocate::make(shared_mem_name, UInt(8), {total_size}, const_true(), s);

        // Define an offset for each allocation. The offsets are in
        // elements, not bytes, so that the stores and loads can use
        // them directly.
        for (int i = (int)(allocations.size()) - 1; i >= 0; i--) {
            Expr offset = 0;
            if (i > 0) {
                offset = Variable::make(Int(32), allocations[i-1].name + ".shared_offset");
                offset += allocations[i-1].size;
                int old_elem_size = allocations[i-1].type.bytes();
                int new_elem_size = allocations[i].type.bytes();
                internal_assert(old_elem_size >= new_elem_size);
                if (old_elem_size != new_elem_size) {
                    // We only have power-of-two sized types.
                    internal_assert(old_elem_size % new_elem_size == 0);
                    offset *= (old_elem_size / new_elem_size);
                }
            }

            s = LetStmt::make(allocations[i].name + ".shared_offset", offset, s);
        }

        return s;
    }

    ExtractSharedAllocations() : in_threads(false) {}
};

class FuseGPUThreadLoops : public IRMutator {
    using IRMutator::visit;

    Scope<Interval> scope;

    void visit(const For *op) {
         if (op->device_api == DeviceAPI::GLSL) {
            stmt = op;
            return;
        }

        user_assert(!(CodeGen_GPU_Dev::is_gpu_thread_var(op->name)))
            << "Loops over GPU thread variable: \"" << op->name
            << "\" is outside of any loop over a GPU block variable. "
            << "This schedule is malformed. There must be a GPU block "
            << "variable, and it must reordered to be outside all GPU "
            << "thread variables.\n";

        bool should_pop = false;
        if (CodeGen_GPU_Dev::is_gpu_block_var(op->name)) {
            Interval im = bounds_of_expr_in_scope(op->min, scope);
            Interval ie = bounds_of_expr_in_scope(op->extent, scope);
            scope.push(op->name, Interval(im.min, im.max + ie.max - 1));
            should_pop = true;
        }

        if (ends_with(op->name, ".__block_id_x")) {

            Stmt body = op->body;

            ExtractBlockSize e;
            body.accept(&e);
            e.max_over_blocks(scope);

            debug(3) << "Fusing thread block:\n" << body << "\n\n";

            NormalizeDimensionality n(e, op->device_api);
            body = n.mutate(body);

            debug(3) << "Normalized dimensionality:\n" << body << "\n\n";

            ExtractSharedAllocations h;
            body = h.mutate(body);

            debug(3) << "Pulled out shared allocations:\n" << body << "\n\n";

            InjectThreadBarriers i;
            body = i.mutate(body);

            debug(3) << "Injected synchronization:\n" << body << "\n\n";

            ReplaceForWithIf f(e);
            body = f.mutate(body);

            debug(3) << "Replaced for with if:\n" << body << "\n\n";

            // Rewrap the whole thing in the loop over threads
            for (int i = 0; i < e.dimensions(); i++) {
                body = For::make("." + thread_names[i], 0, e.extent(i), ForType::Parallel, op->device_api, body);
            }

            // There at least needs to be a loop over __thread_id_x as a marker for codegen
            if (e.dimensions() == 0) {
                body = For::make(".__thread_id_x", 0, 1, ForType::Parallel, op->device_api, body);
            }

            debug(3) << "Rewrapped in for loops:\n" << body << "\n\n";

            // Add back in the shared allocations
            body = h.rewrap(body);

            debug(3) << "Add back in shared allocations:\n" << body << "\n\n";

            if (body.same_as(op->body)) {
                stmt = op;
            } else {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
            }
        } else {
            IRMutator::visit(op);
        }

        if (should_pop) {
            scope.pop(op->name);
        }
    }
};

class ZeroGPULoopMins : public IRMutator {
    bool in_non_glsl_gpu;
    using IRMutator::visit;

    void visit(const For *op) {
        bool old_in_non_glsl_gpu = in_non_glsl_gpu;

        in_non_glsl_gpu = (in_non_glsl_gpu && op->device_api == DeviceAPI::Parent) ||
          (op->device_api == DeviceAPI::CUDA) || (op->device_api == DeviceAPI::OpenCL);

        IRMutator::visit(op);
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && !is_zero(op->min)) {
            op = stmt.as<For>();
            internal_assert(op);
            Expr adjusted = Variable::make(Int(32), op->name) + op->min;
            Stmt body = substitute(op->name, adjusted, op->body);
            stmt = For::make(op->name, 0, op->extent, op->for_type, op->device_api, body);
        }

        in_non_glsl_gpu = old_in_non_glsl_gpu;
    }

public:
    ZeroGPULoopMins() : in_non_glsl_gpu(false) { }
};

Stmt zero_gpu_loop_mins(Stmt s) {
    ZeroGPULoopMins z;
    return z.mutate(s);
}

Stmt fuse_gpu_thread_loops(Stmt s) {
    s = zero_gpu_loop_mins(s);
    FuseGPUThreadLoops f;
    s = f.mutate(s);
    return s;
}

}
}
