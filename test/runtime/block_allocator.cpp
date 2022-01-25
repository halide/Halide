#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"
#include "internal/block_allocator.h"

using namespace Halide::Runtime::Internal;

static size_t allocated_block_memory = 0;
static size_t allocated_region_memory = 0;

static void* allocate_system_memory(void* user_context, size_t bytes) {
    return halide_malloc(user_context, bytes);
}

static void free_system_memory(void* user_context, void* ptr) {
    return halide_free(user_context, ptr);
}

static void allocate_block_memory(void* user_context, MemoryBlock* block) {
    block->handle = halide_malloc(user_context, block->size);
    allocated_block_memory += block->size;
}

static void free_block_memory(void* user_context, MemoryBlock* block) {
    halide_free(user_context, block->handle);
    allocated_block_memory -= block->size;
}

static void allocate_region_memory(void* user_context, MemoryRegion* region) {
    allocated_region_memory += region->size;
}

static void free_region_memory(void* user_context, MemoryRegion* region) {
    allocated_region_memory -= region->size;
}

int main(int argc, char **argv) {
    void* user_context = (void*)1;

    // test class interface
    {
        static const size_t block_size = 1024;
        BlockAllocator::AllocBlockRegionFns functions = {
            allocate_system_memory, free_system_memory,
            allocate_block_memory, free_block_memory,
            allocate_region_memory, free_region_memory
        };

        BlockAllocator allocator(user_context, block_size, functions);

        MemoryRegion* r1 = allocator.reserve(user_context, MemoryAccess::HostOnly, sizeof(int), sizeof(int));
        halide_abort_if_false(user_context, r1 != nullptr);
        halide_abort_if_false(user_context, allocated_block_memory == block_size);
        halide_abort_if_false(user_context, allocated_region_memory == sizeof(int));

        MemoryRegion* r2 = allocator.reserve(user_context, MemoryAccess::HostOnly, sizeof(int), sizeof(int));
        halide_abort_if_false(user_context, r2 != nullptr);
        halide_abort_if_false(user_context, allocated_block_memory == block_size);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * sizeof(int)));

        allocator.reclaim(user_context, r1);
        halide_abort_if_false(user_context, allocated_region_memory == (1 * sizeof(int)));

        allocator.destroy(user_context);
        halide_abort_if_false(user_context, allocated_block_memory == 0);
        halide_abort_if_false(user_context, allocated_region_memory == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
