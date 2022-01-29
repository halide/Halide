#ifndef HALIDE_RUNTIME_MEMORY_ARENA_H
#define HALIDE_RUNTIME_MEMORY_ARENA_H

#include "block_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --
// Memory Arena class for region based allocations and caching of same-type data 
// -- Implementation uses block_storage, and internally manages lists of allocated entries
// -- Customizable allocator (defaults to BlockStorage<T>::default_allocator())
// -- Not thread safe ... locking must be done by client
// 
template<typename T>
class MemoryArena {

    // disable copy constructors and assignment
    MemoryArena(const MemoryArena &) = delete;
    MemoryArena &operator=(const MemoryArena &) = delete;

public:
    static const uint32_t default_capacity = uint32_t(32);

    struct Config {
        uint32_t minimum_block_capacity = default_capacity;
        uint32_t maximum_block_count = 0;
    };

public:
    MemoryArena(void* user_context, const Config& config = default_config(), 
                SystemMemoryAllocator* allocator = default_allocator());

    ~MemoryArena();

    // Factory methods for creation / destruction
    static MemoryArena<T>* create(void* user_context, const Config& config, SystemMemoryAllocator* allocator = default_allocator());
    static void destroy(void* user_context, MemoryArena<T>* arena);

    // Initialize a newly created instance
    void initialize(void* user_context, const Config& config, 
                    SystemMemoryAllocator* allocator = default_allocator());

    // Public interface methods
    T* reserve(void *user_context);
    void reclaim(void *user_context, T* ptr);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);

    // Access methods
    const Config& current_config() const;
    static const Config& default_config();

    SystemMemoryAllocator* current_allocator() const;
    static SystemMemoryAllocator* default_allocator();

private:
    static const uint32_t InvalidEntry = uint32_t(-1);

    // Entry is stored as a union (without padding) ...
    // - stores contents of value (when in use)
    // - stores next free index for block (when not in use)
    union Entry {
        uint32_t free_index;
        alignas(T) char value[sizeof(T)];
    };

    // Each block contains an array of entries with usage info
    // - free index points to next available entry (or InvalidEntry if block is full)
    struct Block {
        Entry* entries;
        AllocationStatus* status;
        uint32_t capacity;
        uint32_t free_index;
    };

    Block& create_block(void *user_context);
    bool collect_block(void *user_context, Block& block); //< returns true if any blocks were removed
    void destroy_block(void* user_context, Block& block);

    Entry* create_entry(void* user_context, Block& block, uint32_t index);
    void destroy_entry(void* user_context, Block& block, uint32_t index);

    T* construct_value(void* user_context, Entry* entry_ptr);
    void destruct_value(void* user_context, Entry* entry_ptr);

private:
    Config config;
    BlockStorage<Block> blocks;
};

template<typename T>
MemoryArena<T>::MemoryArena(void* user_context, 
                            const Config& cfg, 
                            SystemMemoryAllocator* sma
) 
                        :   config(cfg),
                            blocks(sma) {
    halide_debug_assert(user_context, config.minimum_block_capacity > 1);
}

template<typename T>
MemoryArena<T>::~MemoryArena() {
    destroy(nullptr);
}


template<typename T>
MemoryArena<T>* MemoryArena<T>::create(void* user_context, const Config& cfg, SystemMemoryAllocator* allocator) {
    halide_abort_if_false(user_context, allocator != nullptr);
    MemoryArena<T>* result = reinterpret_cast<MemoryArena<T>*>(
        allocator->allocate(user_context, sizeof(MemoryArena<T>))
    );

    if(result == nullptr) {
        halide_error(user_context, "MemoryArena: Failed to create instance! Out of memory!\n");
        return nullptr; 
    }

    result->initialize(user_context, cfg, allocator);
    return result;
}

template<typename T>
void MemoryArena<T>::destroy(void* user_context, MemoryArena<T>* instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    SystemMemoryAllocator* allocator = instance->blocks->current_allocator();
    instance->destroy(user_context);
    halide_abort_if_false(user_context, allocator != nullptr);
    allocator->deallocate(user_context, instance);
}

template<typename T>
void MemoryArena<T>::initialize(void* user_context, 
                                const Config& cfg, 
                                SystemMemoryAllocator* sma) {
    config = cfg;
    blocks.initialize(user_context, sma);
    halide_debug_assert(user_context, config.minimum_block_capacity > 1);
}

template<typename T>
void MemoryArena<T>::destroy(void *user_context) {
    for (size_t i = blocks.size(); i--;) {
        destroy_block(user_context, blocks[i]);
    }
    blocks.destroy(user_context);
}

template<typename T>
bool MemoryArena<T>::collect(void *user_context) {
    bool result = false;
    for (size_t i = blocks.size(); i--;) {
        if(collect_block(user_context, blocks[i])) {
            blocks.remove(user_context, blocks[i]);
            result = true;
        }
    }
    return result;
}

template<typename T>
T* MemoryArena<T>::reserve(void *user_context) {

    // Scan blocks for a free entry
    for (size_t i = blocks.size(); i--;) {
        Block &block = blocks[i];
        if (block.free_index != InvalidEntry) {
            Entry* entry_ptr = create_entry(user_context, block, block.free_index);
            return construct_value(user_context, entry_ptr);
        }
    }

    if(config.maximum_block_count && (blocks.size() >= config.maximum_block_count)) {
        halide_error(user_context, "MemoryArena: Failed to reserve new entry! Maxmimum blocks reached!\n");
        return nullptr;
    }

    // All blocks full ... create a new one
    uint32_t index = 0;
    Block& block = create_block(user_context);
    Entry* entry_ptr = create_entry(user_context, block, index);
    return construct_value(user_context, entry_ptr);
}

template<typename T>
void MemoryArena<T>::reclaim(void *user_context, T* ptr) {
    for (size_t i = blocks.size(); i--;) {
        Block &block = blocks[i];

        // safely cast ptr as entry type
        Entry* entry_ptr;
        memcpy(&entry_ptr, &ptr, sizeof(entry_ptr));

        // is entry_ptr in the address range of this block.
        if ((entry_ptr >= block.entries) && (entry_ptr < block.entries + block.capacity)) {

            const uint32_t index = static_cast<uint32_t>(entry_ptr - block.entries);
            destroy_entry(user_context, block, index);
            return;
        }
    }
    halide_error(user_context, "MemoryArena: Pointer address doesn't belong to this memory pool!\n");
}

template<typename T>
typename MemoryArena<T>::Block &MemoryArena<T>::create_block(void *user_context) {

    // resize capacity starting with initial up to 1.5 last capacity
    const uint32_t new_capacity = blocks.empty() ?
                                      config.minimum_block_capacity :    
                                      (blocks.back().capacity * 3 / 2);

    halide_abort_if_false(user_context, current_allocator() != nullptr );
    AllocationStatus* new_status = (AllocationStatus*)current_allocator()->allocate(user_context, sizeof(AllocationStatus) * new_capacity);
    Entry* new_entries = (Entry*)current_allocator()->allocate(user_context, sizeof(T) * new_capacity);
    const Block new_block = {new_entries, new_status, new_capacity, 0};
    blocks.append(user_context, new_block);

    for (uint32_t i = 0; i < new_capacity - 1; ++i) {
        blocks.back().entries[i].free_index = i + 1;    // singly-linked list of all free entries in the block
        blocks.back().status[i] = AllocationStatus::Available; // usage status
    }

    blocks.back().entries[new_capacity - 1].free_index = InvalidEntry;
    blocks.back().status[new_capacity - 1] = AllocationStatus::InvalidStatus;
    return blocks.back();
}

template<typename T>
void MemoryArena<T>::destroy_block(void* user_context, Block& block) {
    if (block.entries != nullptr) {
        for (size_t i = block.capacity; i--;) {
            if(block.status[i] == AllocationStatus::InUse) {
                destruct_value(user_context, &block.entries[i]);
            }
            block.status[i] = AllocationStatus::InvalidStatus;
        }
        current_allocator()->deallocate(user_context, block.entries);
        current_allocator()->deallocate(user_context, block.status);
        block.status = nullptr;
        block.entries = nullptr;
    }
}

template<typename T>
bool MemoryArena<T>::collect_block(void* user_context, Block& block) {
    if (block.entries != nullptr) {
        bool can_collect = true;
        for (size_t i = block.capacity; i--;) {
            if(block.status[i] == AllocationStatus::InUse) {
                can_collect = false;
                break;
            }
        }
        if(can_collect) {
            destroy_block(user_context, block);
            return true;
        }
    }
    return false;
}

template<typename T>
typename MemoryArena<T>::Entry* MemoryArena<T>::create_entry(void* user_context, Block& block, uint32_t index)
{
    block.status[index] = AllocationStatus::InUse;
    Entry* entry_ptr = &block.entries[index];
    block.free_index = entry_ptr->free_index;
    return entry_ptr;
}

template<typename T>
void MemoryArena<T>::destroy_entry(void* user_context, Block& block, uint32_t index)
{
    Entry* entry_ptr = &block.entries[index];
    if(block.status[index] == AllocationStatus::InUse) {
        destruct_value(user_context, entry_ptr);
    }
    block.status[index] = AllocationStatus::Available;
    entry_ptr->free_index = block.free_index;
    block.free_index = index;
}

template<typename T>
T* MemoryArena<T>::construct_value(void* user_context, Entry* entry_ptr)
{
    T* ptr = (T* )&entry_ptr->value;
#if DEBUG_RUNTIME
    memset(ptr, 0, sizeof(T));
#endif
    return ptr;
}

template<typename T>
void MemoryArena<T>::destruct_value(void* user_context, Entry* entry_ptr)
{
    // EMPTY
}

template<typename T>
const typename MemoryArena<T>::Config& 
MemoryArena<T>::current_config() const {
    return config;
}

template<typename T>
const typename MemoryArena<T>::Config& 
MemoryArena<T>::default_config() {
    static Config result;
    return result;
}

template<typename T>
SystemMemoryAllocator* 
MemoryArena<T>::current_allocator() const {
    return blocks.current_allocator();
}

template<typename T>
SystemMemoryAllocator* 
MemoryArena<T>::default_allocator() {
    return BlockStorage<Block>::default_allocator();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_MEMORY_ARENA_H