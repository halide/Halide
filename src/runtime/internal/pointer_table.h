#ifndef HALIDE_RUNTIME_POINTER_TABLE_H
#define HALIDE_RUNTIME_POINTER_TABLE_H

#include "../HalideRuntime.h"
#include "memory_resources.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Dynamically resizable array for storing untyped pointers
// -- Implementation uses memcpy/memmove for copying
// -- Customizable allocator ... default uses NativeSystemAllocator
class PointerTable {
public:
    static constexpr size_t default_capacity = 32;  // smallish

    explicit PointerTable(void *user_context, size_t initial_capacity = 0, const SystemMemoryAllocatorFns &sma = default_allocator());
    PointerTable(const PointerTable &other);
    ~PointerTable();

    void initialize(void *user_context, size_t initial_capacity = 0, const SystemMemoryAllocatorFns &sma = default_allocator());

    PointerTable &operator=(const PointerTable &other);
    bool operator==(const PointerTable &other) const;
    bool operator!=(const PointerTable &other) const;

    void reserve(void *user_context, size_t capacity, bool free_existing = false);
    void resize(void *user_context, size_t entry_count, bool realloc = true);

    void assign(void *user_context, size_t index, const void *entry_ptr);
    void insert(void *user_context, size_t index, const void *entry_ptr);
    void prepend(void *user_context, const void *entry_ptr);
    void append(void *user_context, const void *entry_ptr);
    void remove(void *user_context, size_t index);

    void fill(void *user_context, const void **array, size_t array_size);
    void insert(void *user_context, size_t index, const void **array, size_t array_size);
    void replace(void *user_context, size_t index, const void **array, size_t array_size);
    void prepend(void *user_context, const void **array, size_t array_size);
    void append(void *user_context, const void **array, size_t array_size);
    void remove(void *user_context, size_t index, size_t entry_count);

    void pop_front(void *user_context);
    void pop_back(void *user_context);
    void shrink_to_fit(void *user_context);
    void clear(void *user_context);
    void destroy(void *user_context);

    bool empty() const;
    size_t size() const;

    void *operator[](size_t index);
    void *operator[](size_t index) const;

    void **data();
    const void **data() const;

    void *front();
    void *back();

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    void allocate(void *user_context, size_t capacity);

    void **ptr = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    SystemMemoryAllocatorFns allocator;
};

PointerTable::PointerTable(void *user_context, size_t initial_capacity, const SystemMemoryAllocatorFns &sma)
    : allocator(sma) {
    halide_debug_assert(user_context, allocator.allocate != nullptr);
    halide_debug_assert(user_context, allocator.deallocate != nullptr);
    if (initial_capacity) {
        reserve(user_context, initial_capacity);
    }
}

PointerTable::PointerTable(const PointerTable &other)
    : PointerTable(nullptr, 0, other.allocator) {
    if (other.capacity) {
        ptr = static_cast<void **>(allocator.allocate(nullptr, other.capacity * sizeof(void *)));
        capacity = other.capacity;
    }
    if (ptr && other.count != 0) {
        count = other.count;
        memcpy(this->ptr, other.ptr, count * sizeof(void *));
    }
}

PointerTable::~PointerTable() {
    destroy(nullptr);
}

void PointerTable::destroy(void *user_context) {
    halide_debug_assert(user_context, allocator.deallocate != nullptr);
    if (ptr != nullptr) {
        allocator.deallocate(user_context, ptr);
    }
    capacity = count = 0;
    ptr = nullptr;
}

void PointerTable::initialize(void *user_context, size_t initial_capacity, const SystemMemoryAllocatorFns &sma) {
    allocator = sma;
    capacity = count = 0;
    ptr = nullptr;
    if (initial_capacity) {
        reserve(user_context, initial_capacity);
    }
}

PointerTable &PointerTable::operator=(const PointerTable &other) {
    if (&other != this) {
        resize(nullptr, other.count);
        if (count != 0 && other.ptr != nullptr) {
            memcpy(ptr, other.ptr, count * sizeof(void *));
        }
    }
    return *this;
}

bool PointerTable::operator==(const PointerTable &other) const {
    if (count != other.count) {
        return false;
    }
    return memcmp(this->ptr, other.ptr, this->size() * sizeof(void *)) == 0;
}

bool PointerTable::operator!=(const PointerTable &other) const {
    return !(*this == other);
}

void PointerTable::fill(void *user_context, const void **array, size_t array_size) {
    if (array_size != 0) {
        resize(user_context, array_size);
        memcpy(this->ptr, array, array_size * sizeof(void *));
        count = array_size;
    }
}

void PointerTable::assign(void *user_context, size_t index, const void *entry_ptr) {
    halide_debug_assert(user_context, index < count);
    ptr[index] = const_cast<void *>(entry_ptr);
}

void PointerTable::prepend(void *user_context, const void *entry_ptr) {
    insert(user_context, 0, &entry_ptr, 1);
}

void PointerTable::append(void *user_context, const void *entry_ptr) {
    append(user_context, &entry_ptr, 1);
}

void PointerTable::pop_front(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    remove(user_context, 0);
}

void PointerTable::pop_back(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    resize(user_context, size() - 1);
}

void PointerTable::clear(void *user_context) {
    resize(user_context, 0);
}

void PointerTable::reserve(void *user_context, size_t new_capacity, bool free_existing) {
    new_capacity = max(new_capacity, count);
    if ((new_capacity < capacity) && !free_existing) {
        new_capacity = capacity;
    }
    allocate(user_context, new_capacity);
}

void PointerTable::resize(void *user_context, size_t entry_count, bool realloc) {
    size_t current_size = capacity;
    size_t requested_size = entry_count;
    size_t minimum_size = default_capacity;
    size_t actual_size = current_size;
    count = requested_size;

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "PointerTable: Resize ("
                        << "requested_size=" << (int32_t)requested_size << " "
                        << "current_size=" << (int32_t)current_size << " "
                        << "minimum_size=" << (int32_t)minimum_size << " "
                        << "sizeof(void*)=" << (int32_t)sizeof(void *) << " "
                        << "realloc=" << (realloc ? "true" : "false") << ")...\n";
#endif

    // increase capacity upto 1.5x existing (or at least min_capacity)
    if (requested_size > current_size) {
        actual_size = max(requested_size, max(current_size * 3 / 2, minimum_size));
    } else if (!realloc) {
        return;
    }

    allocate(user_context, actual_size);
}

void PointerTable::shrink_to_fit(void *user_context) {
    if (capacity > count) {
        void *new_ptr = nullptr;
        if (count > 0) {
            size_t bytes = count * sizeof(void *);
            new_ptr = allocator.allocate(user_context, bytes);
            memcpy(new_ptr, ptr, bytes);
        }
        allocator.deallocate(user_context, ptr);
        capacity = count;
        ptr = static_cast<void **>(new_ptr);
    }
}

void PointerTable::insert(void *user_context, size_t index, const void *entry_ptr) {
    const void *addr = reinterpret_cast<const void *>(entry_ptr);
    insert(user_context, index, &addr, 1);
}

void PointerTable::remove(void *user_context, size_t index) {
    remove(user_context, index, 1);
}

void PointerTable::remove(void *user_context, size_t index, size_t entry_count) {
    halide_debug_assert(user_context, index < count);
    const size_t last_index = size();
    if (index < (last_index - entry_count)) {
        size_t dst_offset = index * sizeof(void *);
        size_t src_offset = (index + entry_count) * sizeof(void *);
        size_t bytes = (last_index - index - entry_count) * sizeof(void *);

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "PointerTable: Remove ("
                            << "index=" << (int32_t)index << " "
                            << "entry_count=" << (int32_t)entry_count << " "
                            << "last_index=" << (int32_t)last_index << " "
                            << "src_offset=" << (int32_t)src_offset << " "
                            << "dst_offset=" << (int32_t)dst_offset << " "
                            << "bytes=" << (int32_t)bytes << ")...\n";
#endif
        memmove(ptr + dst_offset, ptr + src_offset, bytes);
    }
    resize(user_context, last_index - entry_count);
}

void PointerTable::replace(void *user_context, size_t index, const void **array, size_t array_size) {
    halide_debug_assert(user_context, index < count);
    size_t remaining = count - index;
    size_t copy_count = min(remaining, array_size);

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "PointerTable: Replace ("
                        << "index=" << (int32_t)index << " "
                        << "array_size=" << (int32_t)array_size << " "
                        << "remaining=" << (int32_t)remaining << " "
                        << "copy_count=" << (int32_t)copy_count << " "
                        << "capacity=" << (int32_t)capacity << ")...\n";
#endif

    halide_debug_assert(user_context, remaining > 0);
    memcpy(ptr + index, array, copy_count * sizeof(void *));
    count = max(count, index + copy_count);
}

void PointerTable::insert(void *user_context, size_t index, const void **array, size_t array_size) {
    halide_debug_assert(user_context, index <= count);
    const size_t last_index = size();
    resize(user_context, last_index + array_size);
    if (index < last_index) {
        size_t src_offset = index * sizeof(void *);
        size_t dst_offset = (index + array_size) * sizeof(void *);
        size_t bytes = (last_index - index) * sizeof(void *);
        memmove(ptr + dst_offset, ptr + src_offset, bytes);
    }
    replace(user_context, index, array, array_size);
}

void PointerTable::prepend(void *user_context, const void **array, size_t array_size) {
    insert(user_context, 0, array, array_size);
}

void PointerTable::append(void *user_context, const void **array, size_t array_size) {
    const size_t last_index = size();
    insert(user_context, last_index, array, array_size);
}

bool PointerTable::empty() const {
    return count == 0;
}

size_t PointerTable::size() const {
    return count;
}

void *PointerTable::operator[](size_t index) {
    halide_debug_assert(nullptr, index < capacity);
    return ptr[index];
}

void *PointerTable::operator[](size_t index) const {
    halide_debug_assert(nullptr, index < capacity);
    return ptr[index];
}

void **PointerTable::data() {
    return ptr;
}

void *PointerTable::front() {
    halide_debug_assert(nullptr, count > 0);
    return ptr[0];
}

void *PointerTable::back() {
    halide_debug_assert(nullptr, count > 0);
    size_t index = count - 1;
    return ptr[index];
}

const void **PointerTable::data() const {
    return const_cast<const void **>(ptr);
}

void PointerTable::allocate(void *user_context, size_t new_capacity) {
    if (new_capacity != capacity) {
        halide_debug_assert(user_context, allocator.allocate != nullptr);
        size_t bytes = new_capacity * sizeof(void *);

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "PointerTable: Allocating (bytes=" << (int32_t)bytes << " allocator=" << (void *)allocator.allocate << ")...\n";
#endif

        void *new_ptr = bytes ? allocator.allocate(user_context, bytes) : nullptr;
        if (count != 0 && ptr != nullptr && new_ptr != nullptr) {
            memcpy(new_ptr, ptr, count * sizeof(void *));
        }
        if (ptr != nullptr) {
            halide_debug_assert(user_context, allocator.deallocate != nullptr);
            allocator.deallocate(user_context, ptr);
        }
        capacity = new_capacity;
        ptr = static_cast<void **>(new_ptr);
    }
}

const SystemMemoryAllocatorFns &
PointerTable::current_allocator() const {
    return this->allocator;
}

const SystemMemoryAllocatorFns &
PointerTable::default_allocator() {
    static SystemMemoryAllocatorFns native_allocator = {
        native_system_malloc, native_system_free};
    return native_allocator;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_POINTER_TABLE_H
