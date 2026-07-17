#include "HalideRuntime.h"
#include "runtime_atomics.h"

using namespace Halide::Runtime::Internal::Synchronization;

namespace {

thread_local int32_t halide_wasm_thread_id;

}  // namespace

extern "C" {

WEAK int32_t halide_wasm_next_thread_id = 1;

WEAK int32_t halide_current_thread_id() {
    if (halide_wasm_thread_id == 0) {
        halide_wasm_thread_id = atomic_fetch_add_sequentially_consistent(&halide_wasm_next_thread_id, 1);
    }
    return halide_wasm_thread_id;
}
}
