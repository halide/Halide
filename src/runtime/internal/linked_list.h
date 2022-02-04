#ifndef HALIDE_RUNTIME_LINKED_LIST_H
#define HALIDE_RUNTIME_LINKED_LIST_H

#include "memory_arena.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Doubly linked list container
// -- Implemented using MemoryArena<T> for allocation
template<typename T>
class LinkedList {
public:
    // Disable copy support
    LinkedList(const LinkedList &) = delete;
    LinkedList &operator=(const LinkedList &) = delete;

    // Default initial capacity
    static const uint32_t default_capacity = 8;

    // List entry
    struct EntryType {
        T value;
        EntryType *prev_ptr;
        EntryType *next_ptr;
    };

    LinkedList(SystemMemoryAllocator *allocator = default_allocator());
    LinkedList(void *user_context, size_t capacity = default_capacity,
               SystemMemoryAllocator *allocator = default_allocator());
    ~LinkedList();

    void initialize(void *user_context, size_t capacity = default_capacity,
                    SystemMemoryAllocator *allocator = default_allocator());

    EntryType *front();
    EntryType *back();

    const EntryType *front() const;
    const EntryType *back() const;

    EntryType *prepend(void *user_context);
    EntryType *prepend(void *user_context, const T &value);

    EntryType *append(void *user_context);
    EntryType *append(void *user_context, const T &value);

    void pop_front(void *user_context);
    void pop_back(void *user_context);

    EntryType *insert_before(void *user_context, EntryType *entry_ptr);
    EntryType *insert_before(void *user_context, EntryType *entry_ptr, const T &value);

    EntryType *insert_after(void *user_context, EntryType *entry_ptr);
    EntryType *insert_after(void *user_context, EntryType *entry_ptr, const T &value);

    void remove(void *user_context, EntryType *entry_ptr);
    void clear(void *user_context);

    size_t size() const;
    bool empty() const;

    SystemMemoryAllocator *current_allocator() const;
    static SystemMemoryAllocator *default_allocator();

private:
    MemoryArena<EntryType> arena;
    EntryType *front_ptr = nullptr;
    EntryType *back_ptr = nullptr;
    size_t entry_count = 0;
};

template<typename T>
LinkedList<T>::LinkedList(SystemMemoryAllocator *sma)
    : arena(nullptr, {default_capacity, 0}, sma),
      front_ptr(nullptr),
      back_ptr(nullptr) {
    // EMPTY!
}

template<typename T>
LinkedList<T>::LinkedList(void *user_context, size_t capacity,
                          SystemMemoryAllocator *sma)
    : arena(user_context, {default_capacity, 0}, sma),
      front_ptr(nullptr),
      back_ptr(nullptr) {
    // EMPTY!
}

template<typename T>
LinkedList<T>::~LinkedList() {
    clear(nullptr);
}

template<typename T>
void LinkedList<T>::initialize(void *user_context, size_t capacity,
                               SystemMemoryAllocator *sma) {

    arena.initialize(user_context, {default_capacity, 0}, sma);
    front_ptr = nullptr;
    back_ptr = nullptr;
    entry_count = 0;
}

template<typename T>
typename LinkedList<T>::EntryType *LinkedList<T>::front() {
    return front_ptr;
}

template<typename T>
typename LinkedList<T>::EntryType *LinkedList<T>::back() {
    return back_ptr;
}

template<typename T>
const typename LinkedList<T>::EntryType *LinkedList<T>::front() const {
    return front_ptr;
}

template<typename T>
const typename LinkedList<T>::EntryType *LinkedList<T>::back() const {
    return back_ptr;
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::prepend(void *user_context) {
    EntryType *entry_ptr = arena.reserve(user_context);
    entry_ptr->prev_ptr = nullptr;
    if (empty()) {
        entry_ptr->next_ptr = nullptr;
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

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::append(void *user_context) {
    EntryType *entry_ptr = arena.reserve(user_context);
    entry_ptr->next_ptr = nullptr;
    if (empty()) {
        entry_ptr->prev_ptr = nullptr;
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

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::prepend(void *user_context, const T &value) {
    EntryType *entry_ptr = prepend(user_context);
    entry_ptr->value = value;
    return entry_ptr;
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::append(void *user_context, const T &value) {
    EntryType *entry_ptr = append(user_context);
    entry_ptr->value = value;
    return entry_ptr;
}

template<typename T>
void LinkedList<T>::pop_front(void *user_context) {
    halide_abort_if_false(user_context, (entry_count > 0));
    EntryType *remove_ptr = front_ptr;
    EntryType *next_ptr = remove_ptr->next_ptr;
    if (next_ptr != nullptr) {
        next_ptr->prev_ptr = nullptr;
    }
    front_ptr = next_ptr;
    arena.reclaim(user_context, remove_ptr);
    --entry_count;
}

template<typename T>
void LinkedList<T>::pop_back(void *user_context) {
    halide_abort_if_false(user_context, (entry_count > 0));
    EntryType *remove_ptr = back_ptr;
    EntryType *prev_ptr = remove_ptr->prev_ptr;
    if (prev_ptr != nullptr) {
        prev_ptr->next_ptr = nullptr;
    }
    back_ptr = prev_ptr;
    arena.reclaim(user_context, remove_ptr);
    --entry_count;
}

template<typename T>
void LinkedList<T>::clear(void *user_context) {
    if (empty() == false) {
        EntryType *remove_ptr = back_ptr;
        while (remove_ptr != nullptr) {
            EntryType *prev_ptr = remove_ptr->prev_ptr;
            arena.reclaim(user_context, remove_ptr);
            remove_ptr = prev_ptr;
        }
        front_ptr = nullptr;
        back_ptr = nullptr;
        entry_count = 0;
    }
}

template<typename T>
void LinkedList<T>::remove(void *user_context, EntryType *entry_ptr) {
    halide_abort_if_false(user_context, (entry_ptr != nullptr));
    halide_abort_if_false(user_context, (entry_count > 0));

    if (entry_ptr->prev_ptr != nullptr) {
        entry_ptr->prev_ptr->next_ptr = entry_ptr->next_ptr;
    } else {
        halide_abort_if_false(user_context, (front_ptr == entry_ptr));
        front_ptr = entry_ptr->next_ptr;
    }

    if (entry_ptr->next_ptr != nullptr) {
        entry_ptr->next_ptr->prev_ptr = entry_ptr->prev_ptr;
    } else {
        halide_abort_if_false(user_context, (back_ptr == entry_ptr));
        back_ptr = entry_ptr->prev_ptr;
    }

    arena.reclaim(user_context, entry_ptr);
    --entry_count;
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::insert_before(void *user_context, EntryType *entry_ptr) {
    if (entry_ptr != nullptr) {
        EntryType *prev_ptr = entry_ptr->prev_ptr;
        EntryType *new_ptr = arena.reserve(user_context);
        new_ptr->prev_ptr = prev_ptr;
        new_ptr->next_ptr = entry_ptr;
        entry_ptr->prev_ptr = new_ptr;
        if (prev_ptr != nullptr) {
            prev_ptr->next_ptr = new_ptr;
        } else {
            halide_abort_if_false(user_context, (front_ptr == entry_ptr));
            front_ptr = new_ptr;
        }
        ++entry_count;
        return new_ptr;
    } else {
        return append(user_context);
    }
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::insert_after(void *user_context, EntryType *entry_ptr) {
    if (entry_ptr != nullptr) {
        EntryType *next_ptr = entry_ptr->next_ptr;
        EntryType *new_ptr = arena.reserve();
        new_ptr->next_ptr = next_ptr;
        new_ptr->prev_ptr = entry_ptr;
        entry_ptr->next_ptr = new_ptr;
        if (next_ptr != nullptr) {
            next_ptr->prev_ptr = new_ptr;
        } else {
            halide_abort_if_false(user_context, (back_ptr == entry_ptr));
            back_ptr = new_ptr;
        }
        ++entry_count;
        return new_ptr;
    } else {
        return prepend(user_context);
    }
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::insert_before(void *user_context, EntryType *entry_ptr, const T &value) {
    EntryType *new_ptr = insert_before(user_context, entry_ptr);
    new_ptr->value = value;
    return new_ptr;
}

template<typename T>
typename LinkedList<T>::EntryType *
LinkedList<T>::insert_after(void *user_context, EntryType *entry_ptr, const T &value) {
    EntryType *new_ptr = insert_after(user_context, entry_ptr);
    new_ptr->value = value;
    return new_ptr;
}

template<typename T>
size_t LinkedList<T>::size() const {
    return entry_count;
}

template<typename T>
bool LinkedList<T>::empty() const {
    return entry_count == 0;
}

template<typename T>
SystemMemoryAllocator *
LinkedList<T>::current_allocator() const {
    return arena.current_allocator();
}

template<typename T>
SystemMemoryAllocator *
LinkedList<T>::default_allocator() {
    return MemoryArena<T>::default_allocator();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_LINKED_LIST_H