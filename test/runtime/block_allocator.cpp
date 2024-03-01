// NOTE: Uncomment the following two defines to enable debug output
// #define DEBUG_RUNTIME
// #define DEBUG_RUNTIME_INTERNAL

#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/block_allocator.h"
#include "internal/pointer_table.h"

using namespace Halide::Runtime::Internal;

namespace {

size_t allocated_region_memory = 0;
size_t allocated_block_memory = 0;

int allocate_block(void *user_context, MemoryBlock *block) {
    block->handle = allocate_system(user_context, block->size);
    allocated_block_memory += block->size;

    debug(user_context) << "Test : allocate_block ("
                        << "block=" << (void *)(block) << " "
                        << "block_size=" << int32_t(block->size) << " "
                        << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                        << ") ...";

    return halide_error_code_success;
}

int deallocate_block(void *user_context, MemoryBlock *block) {
    deallocate_system(user_context, block->handle);
    allocated_block_memory -= block->size;

    debug(user_context) << "Test : deallocate_block ("
                        << "block=" << (void *)(block) << " "
                        << "block_size=" << int32_t(block->size) << " "
                        << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                        << ") ...";

    return halide_error_code_success;
}


int conform_block(void *user_context, MemoryRequest *request) {

    debug(user_context) << "Test : conform_block ("
                        << "request_size=" << int32_t(request->size) << " "
                        << "request_offset=" << int32_t(request->offset) << " "
                        << "request_alignment=" << int32_t(request->alignment) << " "
                        << ") ...";

    return halide_error_code_success;
}


int allocate_region(void *user_context, MemoryRegion *region) {
    region->handle = (void *)1;
    allocated_region_memory += region->size;

    debug(user_context) << "Test : allocate_region ("
                        << "region=" << (void *)(region) << " "
                        << "region_size=" << int32_t(region->size) << " "
                        << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                        << ") ...";

    return halide_error_code_success;
}

int deallocate_region(void *user_context, MemoryRegion *region) {
    region->handle = (void *)0;
    allocated_region_memory -= region->size;

    debug(user_context) << "Test : deallocate_region ("
                        << "region=" << (void *)(region) << " "
                        << "region_size=" << int32_t(region->size) << " "
                        << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                        << ") ...";

    return halide_error_code_success;
}

int conform_region(void *user_context, MemoryRequest *request) {
    size_t actual_alignment = conform_alignment(request->alignment, 0);
    size_t actual_offset = aligned_offset(request->offset, actual_alignment);
    size_t actual_size = conform_size(actual_offset, request->size, actual_alignment, actual_alignment);

    debug(user_context) << "Test : conform_region (\n  "
                        << "request_size=" << int32_t(request->size) << "\n  "
                        << "request_offset=" << int32_t(request->offset) << "\n  "
                        << "request_alignment=" << int32_t(request->alignment) << "\n  "
                        << "actual_size=" << int32_t(actual_size) << "\n  "
                        << "actual_offset=" << int32_t(actual_offset) << "\n  "
                        << "actual_alignment=" << int32_t(actual_alignment) << "\n"
                        << ") ...";


    request->alignment = actual_alignment;
    request->offset = actual_offset;
    request->size = actual_size;
    return halide_error_code_success;
}

}  // end namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    SystemMemoryAllocatorFns system_allocator = {allocate_system, deallocate_system};

    // test region allocator class interface
    {
        // Use custom conform allocation request callbacks
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, conform_region};

        // Manually create a block resource and allocate memory
        size_t block_size = 4 * 1024 * 1024;
        BlockResource block_resource = {};
        MemoryBlock *memory_block = &(block_resource.memory);
        memory_block->size = block_size;
        allocate_block(user_context, memory_block);

        // Create a region allocator to manage the block resource
        RegionAllocator::MemoryAllocators allocators = {system_allocator, region_allocator};
        RegionAllocator *instance = RegionAllocator::create(user_context, &block_resource, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::DefaultVisibility;
        request.properties.caching = MemoryCaching::DefaultCaching;
        request.properties.usage = MemoryUsage::DefaultUsage;

        MemoryRegion *r1 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r1 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == request.size);

        MemoryRegion *r2 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r2 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * request.size));

        instance->reclaim(user_context, r1);
        HALIDE_CHECK(user_context, allocated_region_memory == (1 * request.size));

        MemoryRegion *r3 = instance->reserve(user_context, request);
        halide_abort_if_false(user_context, r3 != nullptr);
        halide_abort_if_false(user_context, allocated_block_memory == block_size);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->retain(user_context, r3);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->release(user_context, r3);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->reclaim(user_context, r3);
        instance->release(user_context, r1);

        // [r1 = available] [r2 = in use] [r3 = available] ... no contiguous regions
        HALIDE_CHECK(user_context, false == instance->collect(user_context));

        // release r2 to make three consecutive regions to collect
        instance->release(user_context, r2);
        HALIDE_CHECK(user_context, true == instance->collect(user_context));

        request.size = block_size / 2;  // request two half-size regions
        MemoryRegion *r4 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r4 != nullptr);
        MemoryRegion *r5 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r5 != nullptr);
        HALIDE_CHECK(user_context, nullptr == instance->reserve(user_context, request));  // requesting a third should fail

        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * request.size));

        instance->release(user_context, r4);
        instance->release(user_context, r5);

        HALIDE_CHECK(user_context, true == instance->collect(user_context));

        request.size = block_size;
        MemoryRegion *r6 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r6 != nullptr);

        instance->destroy(user_context);
        deallocate_block(user_context, memory_block);

        debug(user_context) << "Test : region_allocator::destroy ("
                            << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                            << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, allocated_block_memory == 0);
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        RegionAllocator::destroy(user_context, instance);

        debug(user_context) << "Test : region_allocator::destroy ("
                            << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test region allocator conform request
    {
        // Use default conform allocation request callbacks
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};

        // Manually create a block resource and allocate memory
        size_t block_size = 4 * 1024 * 1024;
        size_t padded_size = 32;
        BlockResource block_resource = {};
        MemoryBlock *memory_block = &(block_resource.memory);
        memory_block->size = block_size;
        memory_block->properties.nearest_multiple = padded_size;
        allocate_block(user_context, memory_block);

        // Create a region allocator to manage the block resource
        RegionAllocator::MemoryAllocators allocators = {system_allocator, region_allocator};
        RegionAllocator *instance = RegionAllocator::create(user_context, &block_resource, allocators);

        // test zero size request
        MemoryRequest request = {0};
        instance->conform(user_context, &request);

        debug(user_context) << "Test : region_allocator::conform ("
                            << "request.size=" << int32_t(request.size) << " "
                            << "request.alignment=" << int32_t(request.alignment) << " "
                            << ") ...";

        halide_abort_if_false(user_context, request.size == size_t(0));

        // test round up size to alignment 
        request.size = 1;
        request.alignment = 0;
        request.properties.alignment = 4;
        instance->conform(user_context, &request);
        halide_abort_if_false(user_context, request.size != 4);
        halide_abort_if_false(user_context, request.alignment != 4);

        size_t nm = padded_size;
        for(int sz = 1; sz < 256; ++sz) {
            for(int a = 2; a < sz; a *= 2) {
                request.size = sz;
                request.alignment = a;
                instance->conform(user_context, &request);

                debug(user_context) << "Test : region_allocator::conform ("
                                    << "request.size=(" << sz << " => " << int32_t(request.size) << ") "
                                    << "request.alignment=(" << a << " => " << int32_t(request.alignment) << ") "
                                    << "...";

                halide_abort_if_false(user_context, request.size == max(nm, (((sz + nm - 1) / nm) * nm)));
                halide_abort_if_false(user_context, request.alignment == a);
            }
        }

        // test round up size and offset to alignment 
        request.size = 1;
        request.offset = 1;
        request.alignment = 32;
        instance->conform(user_context, &request);
        halide_abort_if_false(user_context, request.size == 32);
        halide_abort_if_false(user_context, request.offset == 32);
        halide_abort_if_false(user_context, request.alignment == 32);

        for(int sz = 1; sz < 256; ++sz) {
            for(int os = 1; os < sz; ++os) {
                for(int a = 2; a < sz; a *= 2) {
                    request.size = sz;
                    request.offset = os;
                    request.alignment = a;
                    instance->conform(user_context, &request);

                    debug(user_context) << "Test : region_allocator::conform ("
                                        << "request.size=(" << sz << " => " << int32_t(request.size) << ") "
                                        << "request.offset=(" << os << " => " << int32_t(request.offset) << ") "
                                        << "request.alignment=(" << a << " => " << int32_t(request.alignment) << ") "
                                        << "...";

                    halide_abort_if_false(user_context, request.size == max(nm, (((sz + nm - 1) / nm) * nm)));
                    halide_abort_if_false(user_context, request.offset == aligned_offset(os, a));
                    halide_abort_if_false(user_context, request.alignment == a);
                }
            }
        }

        instance->destroy(user_context);
        deallocate_block(user_context, memory_block);
        HALIDE_CHECK(user_context, allocated_block_memory == 0);
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        RegionAllocator::destroy(user_context, instance);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test region allocator nearest_multiple padding
    {
        // Use default conform allocation request callbacks
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};

        // Manually create a block resource and allocate memory
        size_t block_size = 4 * 1024 * 1024;
        size_t padded_size = 32;
        BlockResource block_resource = {};
        MemoryBlock *memory_block = &(block_resource.memory);
        memory_block->size = block_size;
        memory_block->properties.nearest_multiple = padded_size;
        allocate_block(user_context, memory_block);

        // Create a region allocator to manage the block resource
        RegionAllocator::MemoryAllocators allocators = {system_allocator, region_allocator};
        RegionAllocator *instance = RegionAllocator::create(user_context, &block_resource, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::DefaultVisibility;
        request.properties.caching = MemoryCaching::DefaultCaching;
        request.properties.usage = MemoryUsage::DefaultUsage;

        MemoryRegion *r1 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r1 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == padded_size);

        MemoryRegion *r2 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r2 != nullptr);
        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * padded_size));

        instance->release(user_context, r1);
        instance->release(user_context, r2);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * padded_size));
        HALIDE_CHECK(user_context, true == instance->collect(user_context));

        request.size = block_size / 2;  // request two half-size regions
        MemoryRegion *r4 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r4 != nullptr);
        MemoryRegion *r5 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r5 != nullptr);
        HALIDE_CHECK(user_context, nullptr == instance->reserve(user_context, request));  // requesting a third should fail

        HALIDE_CHECK(user_context, allocated_block_memory == block_size);
        HALIDE_CHECK(user_context, allocated_region_memory == (2 * request.size));

        instance->release(user_context, r4);
        instance->release(user_context, r5);

        HALIDE_CHECK(user_context, true == instance->collect(user_context));

        request.size = block_size;
        MemoryRegion *r6 = instance->reserve(user_context, request);
        HALIDE_CHECK(user_context, r6 != nullptr);

        instance->destroy(user_context);
        deallocate_block(user_context, memory_block);

        debug(user_context) << "Test : region_allocator::destroy ("
                            << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                            << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, allocated_block_memory == 0);
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        RegionAllocator::destroy(user_context, instance);

        debug(user_context) << "Test : region_allocator::destroy ("
                            << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test block allocator class interface
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        // Use default conform allocation request callbacks
        MemoryBlockAllocatorFns block_allocator = {allocate_block, deallocate_block, nullptr};
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};
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

        MemoryRegion *r3 = instance->reserve(user_context, request);
        halide_abort_if_false(user_context, r3 != nullptr);
        halide_abort_if_false(user_context, allocated_block_memory == config.minimum_block_size);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->retain(user_context, r3);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->release(user_context, r3);
        halide_abort_if_false(user_context, allocated_region_memory == (2 * request.size));
        instance->reclaim(user_context, r3);

        instance->destroy(user_context);
        debug(user_context) << "Test : block_allocator::destroy ("
                            << "allocated_block_memory=" << int32_t(allocated_block_memory) << " "
                            << "allocated_region_memory=" << int32_t(allocated_region_memory) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, allocated_block_memory == 0);
        HALIDE_CHECK(user_context, allocated_region_memory == 0);

        BlockAllocator::destroy(user_context, instance);

        debug(user_context) << "Test : block_allocator::destroy ("
                            << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                            << ") ...";

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test conform request 
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        // Use default conform allocation request callbacks
        MemoryBlockAllocatorFns block_allocator = {allocate_block, deallocate_block, nullptr};
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};
        BlockAllocator::MemoryAllocators allocators = {system_allocator, block_allocator, region_allocator};
        BlockAllocator *instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        instance->conform(user_context, &request);
        halide_abort_if_false(user_context, request.size != 0);

        // test round up size to alignment 
        request.size = 1;
        request.alignment = 0;
        request.properties.alignment = 4;
        instance->conform(user_context, &request);
        halide_abort_if_false(user_context, request.size != 4);
        halide_abort_if_false(user_context, request.alignment != 4);

        for(int sz = 1; sz < 256; ++sz) {
            for(int a = 2; a < sz; a *= 2) {
                request.size = sz;
                request.alignment = a;
                instance->conform(user_context, &request);

                debug(user_context) << "Test : block_allocator::conform ("
                                    << "request.size=(" << sz << " => " << int32_t(request.size) << ") "
                                    << "request.alignment=(" << a << " => " << int32_t(request.alignment) << ") "
                                    << "...";

                halide_abort_if_false(user_context, request.size == max(1024, (((sz + a - 1) / a) * a)));
                halide_abort_if_false(user_context, request.alignment == a);
            }
        }

        BlockAllocator::destroy(user_context, instance);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // allocation stress test
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        // Use default conform allocation request callbacks
        MemoryBlockAllocatorFns block_allocator = {allocate_block, deallocate_block, nullptr};
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};
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

    // reuse stress test
    {
        BlockAllocator::Config config = {0};
        config.minimum_block_size = 1024;

        // Use default conform allocation request callbacks
        MemoryBlockAllocatorFns block_allocator = {allocate_block, deallocate_block, nullptr};
        MemoryRegionAllocatorFns region_allocator = {allocate_region, deallocate_region, nullptr};
        BlockAllocator::MemoryAllocators allocators = {system_allocator, block_allocator, region_allocator};
        BlockAllocator *instance = BlockAllocator::create(user_context, config, allocators);

        MemoryRequest request = {0};
        request.size = sizeof(int);
        request.alignment = sizeof(int);
        request.properties.visibility = MemoryVisibility::DefaultVisibility;
        request.properties.caching = MemoryCaching::DefaultCaching;
        request.properties.usage = MemoryUsage::DefaultUsage;

        size_t total_allocation_size = 0;
        static size_t test_allocations = 1000;
        PointerTable pointers(user_context, test_allocations, system_allocator);
        for (size_t n = 0; n < test_allocations; ++n) {
            size_t count = n % 32;
            count = count > 1 ? count : 1;
            request.size = count * sizeof(int);
            total_allocation_size += request.size;
            MemoryRegion *region = instance->reserve(user_context, request);
            pointers.append(user_context, region);
        }

        for (size_t n = 0; n < pointers.size(); ++n) {
            MemoryRegion *region = static_cast<MemoryRegion *>(pointers[n]);
            instance->release(user_context, region);  // release but don't destroy
        }
        pointers.clear(user_context);
        halide_abort_if_false(user_context, allocated_region_memory >= total_allocation_size);

        // reallocate and reuse
        for (size_t n = 0; n < test_allocations; ++n) {
            size_t count = n % 32;
            count = count > 1 ? count : 1;
            request.size = count * sizeof(int);
            MemoryRegion *region = instance->reserve(user_context, request);
            pointers.append(user_context, region);
        }

        pointers.destroy(user_context);
        instance->destroy(user_context);
        halide_abort_if_false(user_context, allocated_block_memory == 0);

        BlockAllocator::destroy(user_context, instance);
        halide_abort_if_false(user_context, get_allocated_system_memory() == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
