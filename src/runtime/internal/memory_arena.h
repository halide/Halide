#ifndef HALIDE_RUNTIME_MEMORY_ARENA_H
#define HALIDE_RUNTIME_MEMORY_ARENA_H

#include "block_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --
// Memory Arena class for region based allocations and caching of same-type data 
// -- Implementation uses block_storage, and internally manages lists of allocated entries
// -- Customizable initializers for constructing & destructing values
// -- Customizable allocator (defaults to BlockStorage<T>::default_allocator())
// -- Not thread safe ... locking must be done by client
// 
template<typename T>
class MemoryArena {

    // disable copy constructors and assignment
    MemoryArena(const MemoryArena &) = delete;
    MemoryArena &operator=(const MemoryArena &) = delete;

public:
    static const uint32_t InvalidEntry = uint32_t(-1);
    static const uint32_t default_capacity = uint32_t(32);

    typedef void* (*AllocMemoryFn)(void* user_context, size_t bytes);
    typedef void (*FreeMemoryFn)(void* user_context, void* ptr);

    struct AllocatorFns {
        AllocMemoryFn alloc_memory;
        FreeMemoryFn free_memory;
    };
    
    typedef void (*ConstructFn)(void* user_context, T*);
    typedef void (*DestructFn)(void* user_context, T*);
    
    struct InitializerFns {
        ConstructFn construct;
        DestructFn destruct;
    };

    MemoryArena(void* user_context, uint32_t initial_capacity=default_capacity, 
                const AllocatorFns& allocator = default_allocator(),
                const InitializerFns& initializer = default_initializer());

    ~MemoryArena();

    void initialize(void* user_context, uint32_t initial_capacity=default_capacity, 
                    const AllocatorFns& allocator = default_allocator(),
                    const InitializerFns& initializer = default_initializer());

    T* reserve(void *user_context);
    void reclaim(void *user_context, T* ptr);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);

    const InitializerFns& current_initializer() const;
    const AllocatorFns& current_allocator() const;

    static const InitializerFns& default_initializer();
    static const AllocatorFns& default_allocator();

private:
    enum UsageFlag {
        Invalid,
        Available,
        InUse
    };

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
        UsageFlag* flags;
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
    uint32_t capacity;
    InitializerFns initializer;
    BlockStorage<Block> blocks;
};

template<typename T>
MemoryArena<T>::MemoryArena(void* user_context, 
                            uint32_t initial_capacity, 
                            const AllocatorFns& alloc_fns,
                            const InitializerFns& init_fns) : 
    capacity(initial_capacity),
    initializer(init_fns),
    blocks({alloc_fns.alloc_memory, alloc_fns.free_memory}) {

    halide_debug_assert(user_context, capacity > 1);
}

template<typename T>
MemoryArena<T>::~MemoryArena() {
    destroy(nullptr);
}

template<typename T>
void MemoryArena<T>::initialize(void* user_context, 
                                uint32_t initial_capacity, 
                                const AllocatorFns& alloc_fns,
                                const InitializerFns& init_fns) {
    capacity = initial_capacity;
    initializer = init_fns;
    blocks.initialize(user_context, {alloc_fns.alloc_memory, alloc_fns.free_memory});
    halide_debug_assert(user_context, capacity > 1);
}

template<typename T>
void MemoryArena<T>::destroy(void *user_context) {
    for (size_t i = blocks.size(); i--;) {
        destroy_block(user_context, blocks[i]);
    }
    blocks.clear(user_context);
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
    halide_error(user_context, "MemoryArena: Pointer doesn't belong to this memory pool.");
}

template<typename T>
typename MemoryArena<T>::Block &MemoryArena<T>::create_block(void *user_context) {

    // resize capacity starting with initial up to 1.5 last capacity
    const uint32_t new_capacity = blocks.empty() ?
                                      capacity :    
                                      (blocks.back().capacity * 3 / 2);

    UsageFlag* new_flags = (UsageFlag*)current_allocator().alloc_memory(user_context, sizeof(UsageFlag) * new_capacity);
    Entry* new_entries = (Entry*)current_allocator().alloc_memory(user_context, sizeof(T) * new_capacity);
    const Block new_block = {new_entries, new_flags, new_capacity, 0};
    blocks.append(user_context, new_block);

    for (uint32_t i = 0; i < new_capacity - 1; ++i) {
        blocks.back().entries[i].free_index = i + 1;    // singly-linked list of all free entries in the block
        blocks.back().flags[i] = UsageFlag::Available; // usage flags
    }

    blocks.back().entries[new_capacity - 1].free_index = InvalidEntry;
    blocks.back().flags[new_capacity - 1] = UsageFlag::Invalid;
    return blocks.back();
}

template<typename T>
void MemoryArena<T>::destroy_block(void* user_context, Block& block) {
    if (block.entries != nullptr) {
        for (size_t i = block.capacity; i--;) {
            if(block.flags[i] == UsageFlag::InUse) {
                destruct_value(user_context, &block.entries[i]);
            }
            block.flags[i] = UsageFlag::Invalid;
        }
        current_allocator().free_memory(user_context, block.entries);
    }
}

template<typename T>
bool MemoryArena<T>::collect_block(void* user_context, Block& block) {
    if (block.entries != nullptr) {
        bool can_collect = true;
        for (size_t i = block.capacity; i--;) {
            if(block.flags[i] == UsageFlag::InUse) {
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
    block.flags[index] = UsageFlag::InUse;
    Entry* entry_ptr = &block.entries[index];
    block.free_index = entry_ptr->free_index;
    return entry_ptr;
}

template<typename T>
void MemoryArena<T>::destroy_entry(void* user_context, Block& block, uint32_t index)
{
    Entry* entry_ptr = &block.entries[index];
    if(block.flags[index] == UsageFlag::InUse) {
        destruct_value(user_context, entry_ptr);
    }
    block.flags[index] = UsageFlag::Available;
    entry_ptr->free_index = block.free_index;
    block.free_index = index;
}

template<typename T>
T* MemoryArena<T>::construct_value(void* user_context, Entry* entry_ptr)
{
    T* ptr = (T* )&entry_ptr->value;
    if(initializer.construct){ 
        initializer.construct(user_context, ptr); 
    }
    return ptr;
}

template<typename T>
void MemoryArena<T>::destruct_value(void* user_context, Entry* entry_ptr)
{
    if(initializer.destruct){ 
        T* ptr = (T* )&entry_ptr->value;
        initializer.destruct(user_context, ptr); 
    }
}

template<typename T>
const typename MemoryArena<T>::InitializerFns& 
MemoryArena<T>::current_initializer() const {
    return initializer;
}

template<typename T>
const typename MemoryArena<T>::InitializerFns& 
MemoryArena<T>::default_initializer() {
    static InitializerFns empty = {
        nullptr, nullptr
    };
    return empty;
}

template<typename T>
const typename MemoryArena<T>::AllocatorFns& 
MemoryArena<T>::current_allocator() const {
    return reinterpret_cast<const typename MemoryArena<T>::AllocatorFns&>(
        blocks.current_allocator()
    );
}

template<typename T>
const typename MemoryArena<T>::AllocatorFns& 
MemoryArena<T>::default_allocator() {
    return reinterpret_cast<const typename MemoryArena<T>::AllocatorFns&>(
        BlockStorage<Block>::default_allocator()
    );
}


}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_MEMORY_ARENA_H