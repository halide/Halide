#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/block_allocator.h"
#include "internal/pointer_table.h"

using namespace Halide::Runtime::Internal;

namespace {

size_t allocated_region_memory = 0;
size_t allocated_block_memory = 0;

void allocate_block(void *user_context, MemoryBlock *block) {
    block->handle = allocate_system(user_context, block->size);
    allocated_block_memory += block->size;

    debug(user_context) << "Test : allocate_block ("
                        << "block=" << (void *)(block) << " "
                        << "block_size=" << int32_t(block->size) << " "
                        << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                        << ") !\n";
}

void deallocate_block(void *user_context, MemoryBlock *block) {
    deallocate_system(user_context, block->handle);
    allocated_block_memory -= block->size;

    debug(user_context) << "Test : deallocate_block ("
                        << "block=" << (void *)(block) << " "
                        << "block_size=" << int32_t(block->size) << " "
                        << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                        << ") !\n";
}

void allocate_region(void *user_context, MemoryRegion *region) {
    region->handle = (void *)1;
    allocated_region_memory += region->size;

    debug(user_context) << "Test : allocate_region ("
                        << "region=" << (void *)(region) << " "
                        << "region_size=" << int32_t(region->size) << " "
                        << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                        << ") !\n";
}

void deallocate_region(void *user_context, MemoryRegion *region) {
    region->handle = (void *)0;
    allocated_region_memory -= region->size;

    debug(user_context) << "Test : deallocate_region ("
                        << "region=" << (void *)(region) << " "
                        << "region_size=" << int32_t(region->size) << " "
                        << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                        << ") !\n";
}

}  // end namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    SystemMemoryAllocatorFns system_allocator = {allocate_system, deallocate_system};
    MemoryBlockAllocatorFns block_allocator = {allocate_block, deallocate_block};
    MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region};

    // test class interface
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        BlockAllocator::MemoryAllocators allocators = {system_allocator, block_allocator, region_allocator};
        BlockAllocator *instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::DefaultVisibility;
        request.properties.caching = MemoryCaching::DefaultCaching;
        request.properties.usage = MemoryUsage::DefaultUsage;

        MemoryRegion *r1 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r1 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == config.minimum_block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == request.size);

        MemoryRegion *r2 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r2 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == config.minimum_block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * request.size));

        instance->reclaim(user_context, r1);
        HALIDE_CHECK(user_context, allocated_region_memory == (1 * request.size));

        instance->destroy(user_context);
        debug(user_context) << "Test : block_allocator::destroy ("
                            << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                            << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                            << ") !\n";

        HALIDE_CHECK(user_context, allocated_block_memory == 0);
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        BlockAllocator::destroy(user_context, instance);

        debug(user_context) << "Test : block_allocator::destroy ("
                            << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                            << ") !\n";

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // stress test
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        BlockAllocator::MemoryAllocators allocators = {system_allocator, block_allocator, region_allocator};
        BlockAllocator *instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::DefaultVisibility;
        request.properties.caching = MemoryCaching::DefaultCaching;
        request.properties.usage = MemoryUsage::DefaultUsage;

        static size_t test_allocations = 1000;
        PointerTable pointers(user_context, test_allocations, system_allocator);
        for (size_t n = 0; n < test_allocations; ++n) {
            size_t count = n % 32;
            count = count > 1 ? count : 1;
            request.size = count * sizeof(int);
            MemoryRegion *region = instance->reserve(user_context, request);
            pointers.append(user_context, region);
        }

        for (size_t n = 0; n < pointers.size(); ++n) {
            MemoryRegion *region = static_cast<MemoryRegion *>(pointers[n]);
            instance->reclaim(user_context, region);
        }
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        pointers.destroy(user_context);
        instance->destroy(user_context);
        HALIDE_CHECK(user_context, allocated_block_memory == 0);

        BlockAllocator::destroy(user_context, instance);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
