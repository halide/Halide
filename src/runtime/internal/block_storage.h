#ifndef HALIDE_RUNTIME_BLOCK_STORAGE_H
#define HALIDE_RUNTIME_BLOCK_STORAGE_H

#include "memory_resources.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Dynamically resizable array for block storage (eg plain old data)
// -- No usage of constructors/destructors for value type (T)
// -- Implementation uses memcpy/memmove for copying
// -- Customizable allocator ... default uses HalideSystemAllocator
template<typename T>
class BlockStorage {
public:
    static const size_t min_capacity = 8;  // smallish

    BlockStorage(const SystemMemoryAllocatorFns &sma = default_allocator());
    BlockStorage(const BlockStorage<T> &other);
    BlockStorage(void *user_context, const T *array, size_t array_size, const SystemMemoryAllocatorFns &sma = default_allocator());
    BlockStorage(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &sma = default_allocator());
    ~BlockStorage();

    void initialize(void *user_context, const SystemMemoryAllocatorFns &sma = default_allocator());

    BlockStorage &operator=(const BlockStorage &other);
    bool operator==(const BlockStorage &other) const;
    bool operator!=(const BlockStorage &other) const;

    void reserve(void *user_context, size_t capacity, bool free_existing = false);
    void resize(void *user_context, size_t size, bool realloc = true);

    void insert(void *user_context, size_t index, const T &entry);
    void remove(void *user_context, size_t index);
    void prepend(void *user_context, const T &entry);
    void append(void *user_context, const T &entry);

    void assign(void *user_context, const T *array, size_t array_size);
    void insert(void *user_context, size_t index, const T *array, size_t array_size);
    void prepend(void *user_context, const T *array, size_t array_size);
    void append(void *user_context, const T *array, size_t array_size);

    void pop_front(void *user_context);
    void pop_back(void *user_context);
    void shrink_to_fit(void *user_context);
    void clear(void *user_context);
    void destroy(void *user_context);

    bool empty() const;
    size_t size() const;

    T &operator[](size_t index);
    const T &operator[](size_t index) const;

    T *data();
    T &front();
    T &back();

    const T *data() const;
    const T &front() const;
    const T &back() const;

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    void allocate(void *user_context, size_t new_capacity);

    T *ptr = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    SystemMemoryAllocatorFns allocator;
};

template<typename T>
BlockStorage<T>::BlockStorage(const SystemMemoryAllocatorFns &sma)
    : allocator(sma) {
    halide_abort_if_false(nullptr, allocator.allocate != nullptr);
    halide_abort_if_false(nullptr, allocator.deallocate != nullptr);
}

template<typename T>
BlockStorage<T>::BlockStorage(const BlockStorage &other)
    : BlockStorage(other.allocator) {
    ptr = (other.count ? (T *)allocator.allocate(nullptr, other.count * sizeof(T)) : nullptr);
    count = other.count;
    capacity = other.count;
    if (count != 0) {
        memcpy(this->ptr, other.ptr, count * sizeof(T));
    }
}

template<typename T>
BlockStorage<T>::BlockStorage(void *user_context, const T *array, size_t array_size, const SystemMemoryAllocatorFns &sma)
    : BlockStorage(sma) {
    assign(user_context, array, array_size);
}

template<typename T>
BlockStorage<T>::BlockStorage(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &sma)
    : BlockStorage(sma) {
    reserve(user_context, capacity);
}

template<typename T>
BlockStorage<T>::~BlockStorage() {
    destroy(nullptr);
}

template<typename T>
void BlockStorage<T>::destroy(void *user_context) {
    halide_abort_if_false(user_context, allocator.deallocate != nullptr);
    if (ptr != nullptr) {
        allocator.deallocate(user_context, ptr);
    }
    capacity = count = 0;
    ptr = nullptr;
}

template<typename T>
void BlockStorage<T>::initialize(void *user_context, const SystemMemoryAllocatorFns &sma) {
    allocator = sma;
    capacity = count = 0;
    ptr = nullptr;
}

template<typename T>
BlockStorage<T> &BlockStorage<T>::operator=(const BlockStorage &other) {
    if (&other != this) {
        resize(nullptr, other.count);
        if (count != 0 && other.ptr != nullptr) {
            memcpy(ptr, other.ptr, count * sizeof(T));
        }
    }
    return *this;
}

template<typename T>
bool BlockStorage<T>::operator==(const BlockStorage &other) const {
    if (count != other.count) { return false; }
    return memcmp(this->ptr, other.ptr, this->size()) == 0;
}

template<typename T>
bool BlockStorage<T>::operator!=(const BlockStorage &other) const {
    return !(*this == other);
}

template<typename T>
void BlockStorage<T>::assign(void *user_context, const T *array, size_t array_size) {
    if (array_size != 0) {
        resize(user_context, array_size);
        memcpy(this->ptr, array, array_size * sizeof(T));
        count = array_size;
    }
}

template<typename T>
void BlockStorage<T>::prepend(void *user_context, const T &entry) {
    insert(user_context, 0, entry);
}

template<typename T>
void BlockStorage<T>::append(void *user_context, const T &value) {
    const size_t last_index = size();
    resize(user_context, last_index + 1);
    ptr[last_index] = value;
}

template<typename T>
void BlockStorage<T>::pop_front(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    remove(user_context, 0);
}

template<typename T>
void BlockStorage<T>::pop_back(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    resize(user_context, size() - 1);
}

template<typename T>
void BlockStorage<T>::clear(void *user_context) {
    resize(user_context, 0);
}

template<typename T>
void BlockStorage<T>::reserve(void *user_context, size_t new_capacity, bool free_existing) {
    new_capacity = max(new_capacity, count);

    if ((new_capacity < capacity) && !free_existing) {
        new_capacity = capacity;
    }

    allocate(user_context, new_capacity);
}

template<typename T>
void BlockStorage<T>::resize(void *user_context, size_t size, bool realloc) {
    size_t new_capacity = capacity;
    if (size > capacity) {
        // request upto 1.5x existing capacity (or at least min_capacity)
        new_capacity = max(size, max(capacity * 3 / 2, min_capacity));
    } else if (!realloc) {
        count = size;
        return;
    }

    allocate(user_context, new_capacity);
    count = size;
}

template<typename T>
void BlockStorage<T>::shrink_to_fit(void *user_context) {
    if (capacity > count) {
        T *new_ptr = nullptr;
        if (count > 0) {
            size_t bytes = count * sizeof(T);
            new_ptr = allocator.allocate(user_context, bytes);
            memcpy(new_ptr, ptr, bytes);
        }
        allocator.deallocate(user_context, ptr);
        capacity = count;
        ptr = new_ptr;
    }
}

template<typename T>
void BlockStorage<T>::insert(void *user_context, size_t index, const T &value) {
    halide_debug_assert(user_context, index <= count);
    const size_t last_index = size();
    resize(user_context, last_index + 1);
    if (index < last_index) {
        size_t bytes = (last_index - index) * sizeof(T);
        memmove(ptr + (index + 1), ptr + index, bytes);
    }
    ptr[index] = value;
}

template<typename T>
void BlockStorage<T>::remove(void *user_context, size_t index) {
    halide_debug_assert(user_context, index < count);
    const size_t last_index = size();
    if (index < last_index - 1) {
        size_t bytes = (last_index - index - 1) * sizeof(T);
        memmove(ptr + index, ptr + (index + 1), bytes);
    }
    resize(user_context, last_index - 1);
}

template<typename T>
void BlockStorage<T>::insert(void *user_context, size_t index, const T *array, size_t array_size) {
    halide_debug_assert(user_context, index <= count);
    const size_t last_index = size();
    resize(user_context, last_index + array_size);
    if (index < last_index) {
        size_t bytes = (last_index - index) * sizeof(T);
        memmove(ptr + (index + array_size), ptr + index, bytes);
    }
    memcpy(ptr + index, array, array_size * sizeof(T));
}

template<typename T>
void BlockStorage<T>::prepend(void *user_context, const T *array, size_t array_size) {
    insert(user_context, 0, array, array_size);
}

template<typename T>
void BlockStorage<T>::append(void *user_context, const T *array, size_t array_size) {
    const size_t last_index = size();
    resize(user_context, last_index + array_size);
    memcpy(ptr + last_index, array, array_size * sizeof(T));
}

template<typename T>
bool BlockStorage<T>::empty() const {
    return count == 0;
}

template<typename T>
size_t BlockStorage<T>::size() const {
    return count;
}

template<typename T>
T &BlockStorage<T>::operator[](size_t index) {
    halide_debug_assert(nullptr, index < capacity);
    return ptr[index];
}

template<typename T>
const T &BlockStorage<T>::operator[](size_t index) const {
    halide_debug_assert(nullptr, index < capacity);
    return ptr[index];
}

template<typename T>
T *BlockStorage<T>::data() {
    return ptr;
}

template<typename T>
T &BlockStorage<T>::front() {
    halide_debug_assert(nullptr, count > 0);
    return ptr[0];
}

template<typename T>
T &BlockStorage<T>::back() {
    halide_debug_assert(nullptr, count > 0);
    return ptr[count - 1];
}

template<typename T>
const T *BlockStorage<T>::data() const {
    return ptr;
}

template<typename T>
const T &BlockStorage<T>::front() const {
    halide_debug_assert(nullptr, count > 0);
    return ptr[0];
}

template<typename T>
const T &BlockStorage<T>::back() const {
    halide_debug_assert(nullptr, count > 0);
    return ptr[count - 1];
}

template<typename T>
void BlockStorage<T>::allocate(void *user_context, size_t new_capacity) {
    if (new_capacity != capacity) {
        halide_abort_if_false(user_context, allocator.allocate != nullptr);
        T *new_ptr = new_capacity ? (T *)allocator.allocate(user_context, new_capacity * sizeof(T)) : nullptr;
        if (count != 0 && ptr != nullptr && new_ptr != nullptr) {
            memcpy((void *)new_ptr, (void *)ptr, count * sizeof(T));
        }
        if (ptr != nullptr) {
            halide_abort_if_false(user_context, allocator.deallocate != nullptr);
            allocator.deallocate(user_context, ptr);
        }
        capacity = new_capacity;
        ptr = new_ptr;
    }
}

template<typename T>
const SystemMemoryAllocatorFns &
BlockStorage<T>::current_allocator() const {
    return this->allocator;
}

template<typename T>
const SystemMemoryAllocatorFns &
BlockStorage<T>::default_allocator() {
    static SystemMemoryAllocatorFns halide_allocator = {
        native_system_malloc, native_system_free};
    return halide_allocator;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_STORAGE_H