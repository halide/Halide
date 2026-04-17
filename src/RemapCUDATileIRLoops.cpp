#include "RemapCUDATileIRLoops.h"

#include "Debug.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {

class RemapCUDATileIRLoops : public IRMutator {
    using IRMutator::visit;

    // Track whether we're inside a CUDATileIR kernel.
    bool in_cuda_tile_ir = false;

    Stmt visit(const For *op) override {
        bool is_thread_like = op->for_type == ForType::GPUThread ||
                              op->for_type == ForType::GPULane;
        // Enter CUDATileIR context when we see any loop with CUDATileIR device_api.
        if (op->device_api == DeviceAPI::CUDATileIR) {
            ScopedValue<bool> save(in_cuda_tile_ir, true);
            // If this loop itself is a thread/lane loop, remap it too.
            if (is_thread_like) {
                return remap_thread_loop(op);
            }
            return IRMutator::visit(op);
        }

        // Remap thread-like loops inside CUDATileIR kernels (these may have DeviceAPI::None).
        if (in_cuda_tile_ir && is_thread_like) {
            return remap_thread_loop(op);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Allocate *op) override {
        // Tile IR has no local memory model (no alloca equivalent) and we
        // don't yet support shared memory. Force all allocations inside
        // CUDATileIR kernels to MemoryType::Heap so that FuseGPUThreadLoops
        // hoists them to host-side device_malloc and passes the device
        // pointer as a kernel argument. Shared allocations get demoted to
        // heap too (correctness preserved; perf suboptimal).
        if (in_cuda_tile_ir &&
            op->memory_type != MemoryType::Heap) {
            debug(2) << "RemapCUDATileIRLoops: forcing " << op->name
                     << " to MemoryType::Heap\n";
            Stmt body = mutate(op->body);
            return Allocate::make(op->name, op->type, MemoryType::Heap,
                                  op->extents, op->condition, body,
                                  op->new_expr, op->free_function,
                                  op->padding);
        }
        return IRMutator::visit(op);
    }

    // Round up to the next power of two.
    static int next_power_of_two(int x) {
        int p = 1;
        while (p < x) {
            p *= 2;
        }
        return p;
    }

    Stmt remap_thread_loop(const For *op) {
        Stmt body = mutate(op->body);
        // If extent is 1 (min == max), make it serial instead of vectorized.
        if (equal(op->min, op->max)) {
            return For::make(op->name, op->min, op->max,
                             ForType::Serial, op->partition_policy,
                             op->device_api, body);
        }

        Expr extent = simplify(op->extent());
        Expr new_min = op->min;
        Expr new_max = op->max;

        auto extent_val = as_const_int(extent);
        if (!extent_val) {
            // Non-constant extent can't be vectorized. Leave as a serial
            // loop; the Tile IR codegen will emit a ForOp.
            debug(2) << "RemapCUDATileIRLoops: non-constant extent for "
                     << op->name << ", leaving as Serial\n";
            return For::make(op->name, op->min, op->max,
                             ForType::Serial, op->partition_policy,
                             op->device_api, body);
        }


        // Tile IR requires all tile dimensions to be powers of two.
        // If the extent is a known constant that isn't a power of two,
        // pad up and guard the body with an if-statement.
        if (*extent_val > 1 &&
            (*extent_val & (*extent_val - 1)) != 0) {
            int padded = next_power_of_two(static_cast<int>(*extent_val));
            debug(2) << "RemapCUDATileIRLoops: padding " << op->name
                     << " from " << *extent_val << " to " << padded << "\n";
            new_max = new_min + (padded - 1);
            // Guard the body so padded lanes are no-ops.
            Expr var = Variable::make(Int(32), op->name);
            body = IfThenElse::make(var <= op->max, body);
        }

        return For::make(op->name, new_min, new_max,
                         ForType::Vectorized, op->partition_policy,
                         op->device_api, body);
    }
};

}  // namespace

Stmt remap_cuda_tile_ir_loops(const Stmt &s) {
    return RemapCUDATileIRLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
