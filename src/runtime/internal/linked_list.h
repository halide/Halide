#ifndef HALIDE_RUNTIME_LINKED_LIST_H
#define HALIDE_RUNTIME_LINKED_LIST_H

#include "HalideRuntime.h"
#include "memory_arena.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Doubly linked list container
// -- Implemented using MemoryArena for allocation
class LinkedList {
public:
    // Disable copy support
    LinkedList(const LinkedList &) = delete;
    LinkedList &operator=(const LinkedList &) = delete;

    // Default initial capacity
    static constexpr uint32_t default_capacity = uint32_t(32);  // smallish

    // List entry
    struct EntryType {
        void *value = nullptr;
        EntryType *prev_ptr = nullptr;
        EntryType *next_ptr = nullptr;
    };

    LinkedList(void *user_context, uint32_t entry_size, uint32_t capacity = default_capacity,
               const SystemMemoryAllocatorFns &allocator = default_allocator());
    ~LinkedList();

    void initialize(void *user_context, uint32_t entry_size, uint32_t capacity = default_capacity,
                    const SystemMemoryAllocatorFns &allocator = default_allocator());

    EntryType *front();
    EntryType *back();

    const EntryType *front() const;
    const EntryType *back() const;

    EntryType *prepend(void *user_context);
    EntryType *prepend(void *user_context, const void *value);

    EntryType *append(void *user_context);
    EntryType *append(void *user_context, const void *value);

    void pop_front(void *user_context);
    void pop_back(void *user_context);

    EntryType *insert_before(void *user_context, EntryType *entry_ptr);
    EntryType *insert_before(void *user_context, EntryType *entry_ptr, const void *value);

    EntryType *insert_after(void *user_context, EntryType *entry_ptr);
    EntryType *insert_after(void *user_context, EntryType *entry_ptr, const void *value);

    void remove(void *user_context, EntryType *entry_ptr);
    void clear(void *user_context);
    void destroy(void *user_context);

    size_t size() const;
    bool empty() const;

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    EntryType *reserve(void *user_context);
    void reclaim(void *user_context, EntryType *entry_ptr);

    MemoryArena *link_arena = nullptr;
    MemoryArena *data_arena = nullptr;
    EntryType *front_ptr = nullptr;
    EntryType *back_ptr = nullptr;
    size_t entry_count = 0;
};

LinkedList::LinkedList(void *user_context, uint32_t entry_size, uint32_t capacity,
                       const SystemMemoryAllocatorFns &sma) {
    uint32_t arena_capacity = max(capacity, MemoryArena::default_capacity);
    link_arena = MemoryArena::create(user_context, {sizeof(EntryType), arena_capacity, 0}, sma);
    data_arena = MemoryArena::create(user_context, {entry_size, arena_capacity, 0}, sma);
    front_ptr = nullptr;
    back_ptr = nullptr;
    entry_count = 0;
}

LinkedList::~LinkedList() {
    destroy(nullptr);
}

void LinkedList::initialize(void *user_context, uint32_t entry_size, uint32_t capacity,
                            const SystemMemoryAllocatorFns &sma) {
    uint32_t arena_capacity = max(capacity, MemoryArena::default_capacity);
    link_arena = MemoryArena::create(user_context, {sizeof(EntryType), arena_capacity, 0}, sma);
    data_arena = MemoryArena::create(user_context, {entry_size, arena_capacity, 0}, sma);
    front_ptr = nullptr;
    back_ptr = nullptr;
    entry_count = 0;
}

void LinkedList::destroy(void *user_context) {
    clear(nullptr);
    if (link_arena) {
        MemoryArena::destroy(nullptr, link_arena);
    }
    if (data_arena) {
        MemoryArena::destroy(nullptr, data_arena);
    }
    link_arena = nullptr;
    data_arena = nullptr;
    front_ptr = nullptr;
    back_ptr = nullptr;
    entry_count = 0;
}

typename LinkedList::EntryType *LinkedList::front() {
    return front_ptr;
}

typename LinkedList::EntryType *LinkedList::back() {
    return back_ptr;
}

const typename LinkedList::EntryType *LinkedList::front() const {
    return front_ptr;
}

const typename LinkedList::EntryType *LinkedList::back() const {
    return back_ptr;
}

typename LinkedList::EntryType *
LinkedList::prepend(void *user_context) {
    EntryType *entry_ptr = reserve(user_context);
    if (empty()) {
        front_ptr = entry_ptr;
        back_ptr = entry_ptr;
        entry_count = 1;
    } else {
        entry_ptr->next_ptr = front_ptr;
        front_ptr->prev_ptr = entry_ptr;
        front_ptr = entry_ptr;
        ++entry_count;
    }
    return entry_ptr;
}

typename LinkedList::EntryType *
LinkedList::append(void *user_context) {
    EntryType *entry_ptr = reserve(user_context);
    if (empty()) {
        front_ptr = entry_ptr;
        back_ptr = entry_ptr;
        entry_count = 1;
    } else {
        entry_ptr->prev_ptr = back_ptr;
        back_ptr->next_ptr = entry_ptr;
        back_ptr = entry_ptr;
        ++entry_count;
    }
    return entry_ptr;
}

typename LinkedList::EntryType *
LinkedList::prepend(void *user_context, const void *value) {
    EntryType *entry_ptr = prepend(user_context);
    memcpy(entry_ptr->value, value, data_arena->current_config().entry_size);
    return entry_ptr;
}

typename LinkedList::EntryType *
LinkedList::append(void *user_context, const void *value) {
    EntryType *entry_ptr = append(user_context);
    memcpy(entry_ptr->value, value, data_arena->current_config().entry_size);
    return entry_ptr;
}

void LinkedList::pop_front(void *user_context) {
    halide_debug_assert(user_context, (entry_count > 0));
    EntryType *remove_ptr = front_ptr;
    EntryType *next_ptr = remove_ptr->next_ptr;
    if (next_ptr != nullptr) {
        next_ptr->prev_ptr = nullptr;
    }
    front_ptr = next_ptr;
    reclaim(user_context, remove_ptr);
    --entry_count;
}

void LinkedList::pop_back(void *user_context) {
    halide_debug_assert(user_context, (entry_count > 0));
    EntryType *remove_ptr = back_ptr;
    EntryType *prev_ptr = remove_ptr->prev_ptr;
    if (prev_ptr != nullptr) {
        prev_ptr->next_ptr = nullptr;
    }
    back_ptr = prev_ptr;
    reclaim(user_context, remove_ptr);
    --entry_count;
}

void LinkedList::clear(void *user_context) {
    if (empty() == false) {
        EntryType *remove_ptr = back_ptr;
        while (remove_ptr != nullptr) {
            EntryType *prev_ptr = remove_ptr->prev_ptr;
            reclaim(user_context, remove_ptr);
            remove_ptr = prev_ptr;
        }
        front_ptr = nullptr;
        back_ptr = nullptr;
        entry_count = 0;
    }
}

void LinkedList::remove(void *user_context, EntryType *entry_ptr) {
    halide_debug_assert(user_context, (entry_ptr != nullptr));
    halide_debug_assert(user_context, (entry_count > 0));

    if (entry_ptr->prev_ptr != nullptr) {
        entry_ptr->prev_ptr->next_ptr = entry_ptr->next_ptr;
    } else {
        halide_debug_assert(user_context, (front_ptr == entry_ptr));
        front_ptr = entry_ptr->next_ptr;
    }

    if (entry_ptr->next_ptr != nullptr) {
        entry_ptr->next_ptr->prev_ptr = entry_ptr->prev_ptr;
    } else {
        halide_debug_assert(user_context, (back_ptr == entry_ptr));
        back_ptr = entry_ptr->prev_ptr;
    }

    reclaim(user_context, entry_ptr);
    --entry_count;
}

typename LinkedList::EntryType *
LinkedList::insert_before(void *user_context, EntryType *entry_ptr) {
    if (entry_ptr != nullptr) {
        EntryType *prev_ptr = entry_ptr->prev_ptr;
        EntryType *new_ptr = reserve(user_context);
        new_ptr->prev_ptr = prev_ptr;
        new_ptr->next_ptr = entry_ptr;
        entry_ptr->prev_ptr = new_ptr;
        if (prev_ptr != nullptr) {
            prev_ptr->next_ptr = new_ptr;
        } else {
            halide_debug_assert(user_context, (front_ptr == entry_ptr));
            front_ptr = new_ptr;
        }
        ++entry_count;
        return new_ptr;
    } else {
        return append(user_context);
    }
}

typename LinkedList::EntryType *
LinkedList::insert_after(void *user_context, EntryType *entry_ptr) {
    if (entry_ptr != nullptr) {
        EntryType *next_ptr = entry_ptr->next_ptr;
        EntryType *new_ptr = reserve(user_context);
        new_ptr->next_ptr = next_ptr;
        new_ptr->prev_ptr = entry_ptr;
        entry_ptr->next_ptr = new_ptr;
        if (next_ptr != nullptr) {
            next_ptr->prev_ptr = new_ptr;
        } else {
            halide_debug_assert(user_context, (back_ptr == entry_ptr));
            back_ptr = new_ptr;
        }
        ++entry_count;
        return new_ptr;
    } else {
        return prepend(user_context);
    }
}

typename LinkedList::EntryType *
LinkedList::insert_before(void *user_context, EntryType *entry_ptr, const void *value) {
    EntryType *new_ptr = insert_before(user_context, entry_ptr);
    memcpy(new_ptr->value, value, data_arena->current_config().entry_size);
    return new_ptr;
}

typename LinkedList::EntryType *
LinkedList::insert_after(void *user_context, EntryType *entry_ptr, const void *value) {
    EntryType *new_ptr = insert_after(user_context, entry_ptr);
    memcpy(new_ptr->value, value, data_arena->current_config().entry_size);
    return new_ptr;
}

size_t LinkedList::size() const {
    return entry_count;
}

bool LinkedList::empty() const {
    return entry_count == 0;
}

const SystemMemoryAllocatorFns &
LinkedList::current_allocator() const {
    return link_arena->current_allocator();
}

const SystemMemoryAllocatorFns &
LinkedList::default_allocator() {
    return MemoryArena::default_allocator();
}

typename LinkedList::EntryType *
LinkedList::reserve(void *user_context) {
    EntryType *entry_ptr = static_cast<EntryType *>(
        link_arena->reserve(user_context, true));
    entry_ptr->value = data_arena->reserve(user_context, true);
    entry_ptr->next_ptr = nullptr;
    entry_ptr->prev_ptr = nullptr;
    return entry_ptr;
}

void LinkedList::reclaim(void *user_context, EntryType *entry_ptr) {
    void *value_ptr = entry_ptr->value;
    entry_ptr->value = nullptr;
    entry_ptr->next_ptr = nullptr;
    entry_ptr->prev_ptr = nullptr;
    data_arena->reclaim(user_context, value_ptr);
    link_arena->reclaim(user_context, entry_ptr);
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_LINKED_LIST_H
