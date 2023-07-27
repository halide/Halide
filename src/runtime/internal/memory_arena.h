#ifndef HALIDE_RUNTIME_MEMORY_ARENA_H
#define HALIDE_RUNTIME_MEMORY_ARENA_H

#include "../HalideRuntime.h"
#include "block_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --
// Memory Arena class for region based allocations and caching of same-type data
// -- Implementation uses block_storage, and internally manages lists of allocated entries
// -- Customizable allocator (defaults to BlockStorage::default_allocator())
// -- Not thread safe ... locking must be done by client
//
class MemoryArena {
public:
    // Disable copy constructors and assignment
    MemoryArena(const MemoryArena &) = delete;
    MemoryArena &operator=(const MemoryArena &) = delete;

    // Default initial capacity
    static constexpr uint32_t default_capacity = uint32_t(32);  // smallish

    // Configurable parameters
    struct Config {
        uint32_t entry_size = 1;
        uint32_t minimum_block_capacity = default_capacity;
        uint32_t maximum_block_count = 0;
    };

    explicit MemoryArena(void *user_context, const Config &config = default_config(),
                         const SystemMemoryAllocatorFns &allocator = default_allocator());

    ~MemoryArena();

    // Factory methods for creation / destruction
    static MemoryArena *create(void *user_context, const Config &config, const SystemMemoryAllocatorFns &allocator = default_allocator());
    static void destroy(void *user_context, MemoryArena *arena);

    // Initialize a newly created instance
    void initialize(void *user_context, const Config &config,
                    const SystemMemoryAllocatorFns &allocator = default_allocator());

    // Public interface methods
    void *reserve(void *user_context, bool initialize = false);
    void reclaim(void *user_context, void *ptr);
    bool collect(void *user_context);  //< returns true if any blocks were removed
    void destroy(void *user_context);

    // Access methods
    const Config &current_config() const;
    static const Config &default_config();

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    // Sentinal invalid entry value
    static const uint32_t invalid_entry = uint32_t(-1);

    // Each block contains:
    // - an array of entries
    // - an array of indices (for the free list)
    // - an array of status flags (indicating usage)
    // - free index points to next available entry for the block (or invalid_entry if block is full)
    struct Block {
        void *entries = nullptr;
        uint32_t *indices = nullptr;
        AllocationStatus *status = nullptr;
        uint32_t capacity = 0;
        uint32_t free_index = 0;
    };

    Block *create_block(void *user_context);
    bool collect_block(void *user_context, Block *block);  //< returns true if any blocks were removed
    void destroy_block(void *user_context, Block *block);
    Block *lookup_block(void *user_context, uint32_t index);

    void *create_entry(void *user_context, Block *block, uint32_t index);
    void destroy_entry(void *user_context, Block *block, uint32_t index);
    void *lookup_entry(void *user_context, Block *block, uint32_t index);

    Config config;
    BlockStorage blocks;
};

MemoryArena::MemoryArena(void *user_context,
                         const Config &cfg,
                         const SystemMemoryAllocatorFns &alloc)
    : config(cfg),
      blocks(user_context, {sizeof(MemoryArena::Block), 32, 32}, alloc) {
    halide_debug_assert(user_context, config.minimum_block_capacity > 1);
}

MemoryArena::~MemoryArena() {
    destroy(nullptr);
}

MemoryArena *MemoryArena::create(void *user_context, const Config &cfg, const SystemMemoryAllocatorFns &system_allocator) {
    halide_debug_assert(user_context, system_allocator.allocate != nullptr);
    MemoryArena *result = reinterpret_cast<MemoryArena *>(
        system_allocator.allocate(user_context, sizeof(MemoryArena)));

    if (result == nullptr) {
        halide_error(user_context, "MemoryArena: Failed to create instance! Out of memory!\n");
        return nullptr;
    }

    result->initialize(user_context, cfg, system_allocator);
    return result;
}

void MemoryArena::destroy(void *user_context, MemoryArena *instance) {
    halide_debug_assert(user_context, instance != nullptr);
    const SystemMemoryAllocatorFns &system_allocator = instance->blocks.current_allocator();
    instance->destroy(user_context);
    halide_debug_assert(user_context, system_allocator.deallocate != nullptr);
    system_allocator.deallocate(user_context, instance);
}

void MemoryArena::initialize(void *user_context,
                             const Config &cfg,
                             const SystemMemoryAllocatorFns &system_allocator) {
    config = cfg;
    blocks.initialize(user_context, {sizeof(MemoryArena::Block), 32, 32}, system_allocator);
    halide_debug_assert(user_context, config.minimum_block_capacity > 1);
}

void MemoryArena::destroy(void *user_context) {
    if (!blocks.empty()) {
        for (size_t i = blocks.size(); i--;) {
            Block *block = lookup_block(user_context, i);
            halide_debug_assert(user_context, block != nullptr);
            destroy_block(user_context, block);
        }
    }
    blocks.destroy(user_context);
}

bool MemoryArena::collect(void *user_context) {
    bool result = false;
    for (size_t i = blocks.size(); i--;) {
        Block *block = lookup_block(user_context, i);
        halide_debug_assert(user_context, block != nullptr);
        if (collect_block(user_context, block)) {
            blocks.remove(user_context, i);
            result = true;
        }
    }
    return result;
}

void *MemoryArena::reserve(void *user_context, bool initialize) {
    // Scan blocks for a free entry
    for (size_t i = blocks.size(); i--;) {
        Block *block = lookup_block(user_context, i);
        halide_debug_assert(user_context, block != nullptr);
        if (block->free_index != invalid_entry) {
            return create_entry(user_context, block, block->free_index);
        }
    }

    if (config.maximum_block_count && (blocks.size() >= config.maximum_block_count)) {
        halide_error(user_context, "MemoryArena: Failed to reserve new entry! Maxmimum blocks reached!\n");
        return nullptr;
    }

    // All blocks full ... create a new one
    uint32_t index = 0;
    Block *block = create_block(user_context);
    void *entry_ptr = create_entry(user_context, block, index);

    // Optionally clear the allocation if requested
    if (initialize) {
        memset(entry_ptr, 0, config.entry_size);
    }
    return entry_ptr;
}

void MemoryArena::reclaim(void *user_context, void *entry_ptr) {
    for (size_t i = blocks.size(); i--;) {
        Block *block = lookup_block(user_context, i);
        halide_debug_assert(user_context, block != nullptr);

        // is entry_ptr in the address range of this block.
        uint8_t *offset_ptr = static_cast<uint8_t *>(entry_ptr);
        uint8_t *base_ptr = static_cast<uint8_t *>(block->entries);
        uint8_t *end_ptr = static_cast<uint8_t *>(offset_address(block->entries, block->capacity * config.entry_size));
        if ((entry_ptr >= base_ptr) && (entry_ptr < end_ptr)) {
            const uint32_t offset = static_cast<uint32_t>(offset_ptr - base_ptr);
            const uint32_t index = offset / config.entry_size;
            destroy_entry(user_context, block, index);
            return;
        }
    }
    halide_error(user_context, "MemoryArena: Pointer address doesn't belong to this memory pool!\n");
}

typename MemoryArena::Block *MemoryArena::create_block(void *user_context) {
    // resize capacity starting with initial up to 1.5 last capacity
    uint32_t new_capacity = config.minimum_block_capacity;
    if (!blocks.empty()) {
        const Block *last_block = static_cast<Block *>(blocks.back());
        new_capacity = (last_block->capacity * 3 / 2);
    }

    halide_debug_assert(user_context, current_allocator().allocate != nullptr);
    void *new_entries = current_allocator().allocate(user_context, config.entry_size * new_capacity);
    memset(new_entries, 0, config.entry_size * new_capacity);

    uint32_t *new_indices = (uint32_t *)current_allocator().allocate(user_context, sizeof(uint32_t) * new_capacity);
    AllocationStatus *new_status = (AllocationStatus *)current_allocator().allocate(user_context, sizeof(AllocationStatus) * new_capacity);

    for (uint32_t i = 0; i < new_capacity - 1; ++i) {
        new_indices[i] = i + 1;                       // singly-linked list of all free entries in the block
        new_status[i] = AllocationStatus::Available;  // usage status
    }

    new_indices[new_capacity - 1] = invalid_entry;
    new_status[new_capacity - 1] = AllocationStatus::InvalidStatus;

    const Block new_block = {new_entries, new_indices, new_status, new_capacity, 0};
    blocks.append(user_context, &new_block);
    return static_cast<Block *>(blocks.back());
}

void MemoryArena::destroy_block(void *user_context, Block *block) {
    halide_debug_assert(user_context, block != nullptr);
    if (block->entries != nullptr) {
        halide_debug_assert(user_context, current_allocator().deallocate != nullptr);
        current_allocator().deallocate(user_context, block->entries);
        current_allocator().deallocate(user_context, block->indices);
        current_allocator().deallocate(user_context, block->status);
        block->entries = nullptr;
        block->indices = nullptr;
        block->status = nullptr;
    }
}

bool MemoryArena::collect_block(void *user_context, Block *block) {
    halide_debug_assert(user_context, block != nullptr);
    if (block->entries != nullptr) {
        bool can_collect = true;
        for (size_t i = block->capacity; i--;) {
            if (block->status[i] == AllocationStatus::InUse) {
                can_collect = false;
                break;
            }
        }
        if (can_collect) {
            destroy_block(user_context, block);
            return true;
        }
    }
    return false;
}

MemoryArena::Block *MemoryArena::lookup_block(void *user_context, uint32_t index) {
    return static_cast<Block *>(blocks[index]);
}

void *MemoryArena::lookup_entry(void *user_context, Block *block, uint32_t index) {
    halide_debug_assert(user_context, block != nullptr);
    halide_debug_assert(user_context, block->entries != nullptr);
    return offset_address(block->entries, index * config.entry_size);
}

void *MemoryArena::create_entry(void *user_context, Block *block, uint32_t index) {
    void *entry_ptr = lookup_entry(user_context, block, index);
    block->free_index = block->indices[index];
    block->status[index] = AllocationStatus::InUse;
#if DEBUG_RUNTIME_INTERNAL
    memset(entry_ptr, 0, config.entry_size);
#endif
    return entry_ptr;
}

void MemoryArena::destroy_entry(void *user_context, Block *block, uint32_t index) {
    block->status[index] = AllocationStatus::Available;
    block->indices[index] = block->free_index;
    block->free_index = index;
}

const typename MemoryArena::Config &
MemoryArena::current_config() const {
    return config;
}

const typename MemoryArena::Config &
MemoryArena::default_config() {
    static Config result;
    return result;
}

const SystemMemoryAllocatorFns &
MemoryArena::current_allocator() const {
    return blocks.current_allocator();
}

const SystemMemoryAllocatorFns &
MemoryArena::default_allocator() {
    return BlockStorage::default_allocator();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_MEMORY_ARENA_H
