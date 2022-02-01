#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"
#include "internal/block_allocator.h"

using namespace Halide::Runtime::Internal;


class TestBlockAllocator : public MemoryBlockAllocator {
public:
    size_t allocated_block_memory = 0;
    
    void allocate(void* user_context, MemoryBlock* block) override {
        block->handle = halide_malloc(user_context, block->size);
        allocated_block_memory += block->size;
    }

    void deallocate(void* user_context, MemoryBlock* block) override {
        halide_free(user_context, block->handle);
        allocated_block_memory -= block->size;
    }
};

class TestRegionAllocator : public MemoryRegionAllocator {
public:
    size_t allocated_region_memory = 0;

    void allocate(void* user_context, MemoryRegion* region) override {
        region->handle = (void*)1;
        allocated_region_memory += region->size;
    }

    void deallocate(void* user_context, MemoryRegion* region) override {
        region->handle = (void*)0;
        allocated_region_memory -= region->size;
    }
};


int main(int argc, char **argv) {
    void* user_context = (void*)1;

    // test class interface
    {
        HalideSystemAllocator system_allocator;
        TestBlockAllocator block_allocator;
        TestRegionAllocator region_allocator;
        
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        BlockAllocator::MemoryAllocators allocators = { &system_allocator, &block_allocator, &region_allocator };
        BlockAllocator* instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::UnknownVisibility;
        request.properties.caching = MemoryCaching::UnknownCaching;
        request.properties.mutability = MemoryMutability::UnknownMutability;
        
        MemoryRegion* r1 = instance->reserve(user_context, request);
        halide_abort_if_false(user_context, r1 != nullptr);
        halide_abort_if_false(user_context, block_allocator.allocated_block_memory == config.minimum_block_size);
        halide_abort_if_false(user_context, region_allocator.allocated_region_memory == request.size);

        MemoryRegion* r2 = instance->reserve(user_context, request);
        halide_abort_if_false(user_context, r2 != nullptr);
        halide_abort_if_false(user_context, block_allocator.allocated_block_memory == config.minimum_block_size);
        halide_abort_if_false(user_context, region_allocator.allocated_region_memory == (2 * request.size));

        instance->reclaim(user_context, r1);
        halide_abort_if_false(user_context, region_allocator.allocated_region_memory == (1 * request.size));

        instance->destroy(user_context);
        halide_abort_if_false(user_context, block_allocator.allocated_block_memory == 0);
        halide_abort_if_false(user_context, region_allocator.allocated_region_memory == 0);

        BlockAllocator::destroy(user_context, instance);
    }

    // stress test
    {
        HalideSystemAllocator system_allocator;
        TestBlockAllocator block_allocator;
        TestRegionAllocator region_allocator;
        
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        BlockAllocator::MemoryAllocators allocators = { &system_allocator, &block_allocator, &region_allocator };
        BlockAllocator* instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::UnknownVisibility;
        request.properties.caching = MemoryCaching::UnknownCaching;
        request.properties.mutability = MemoryMutability::UnknownMutability;

        static size_t test_allocations = 1000;
        BlockStorage<MemoryRegion*> regions(user_context, test_allocations, &system_allocator);
        for(size_t n = 0; n < test_allocations; ++n) {
            size_t count = n % 32;
            count = count > 1 ? count : 1;
            request.size = count * sizeof(int); 
            MemoryRegion* region = instance->reserve(user_context, request);
            regions.append(user_context, region);            
        }

        for(size_t n = 0; n < regions.size(); ++n) {
            instance->reclaim(user_context, regions[n]);
        }
        halide_abort_if_false(user_context, region_allocator.allocated_region_memory == 0);

        instance->destroy(user_context);
        halide_abort_if_false(user_context, block_allocator.allocated_block_memory == 0);

        BlockAllocator::destroy(user_context, instance);
    }

    print(user_context) << "Success!\n";
    return 0;
}
