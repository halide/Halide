#ifndef HALIDE_RUNTIME_BLOCK_STORAGE_H
#define HALIDE_RUNTIME_BLOCK_STORAGE_H

#include "HalideRuntime.h"
#include "memory_resources.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Dynamically resizable array for block storage (eg plain old data)
// -- No usage of constructors/destructors for value type
// -- Assumes all elements stored are uniformly the same fixed size
// -- Allocations are done in blocks of a fixed size
// -- Implementation uses memcpy/memmove for copying
// -- Customizable allocator ... default uses NativeSystemAllocator
class BlockStorage {
public:
    static constexpr size_t default_capacity = 32;  // smallish

    // Configurable parameters
    struct Config {
        uint32_t entry_size = 1;   // bytes per entry
        uint32_t block_size = 32;  // bytes per each allocation block
        uint32_t minimum_capacity = default_capacity;
    };

    BlockStorage(void *user_context, const Config &cfg, const SystemMemoryAllocatorFns &sma = default_allocator());
    BlockStorage(const BlockStorage &other);
    ~BlockStorage();

    void initialize(void *user_context, const Config &cfg, const SystemMemoryAllocatorFns &sma = default_allocator());

    BlockStorage &operator=(const BlockStorage &other);
    bool operator==(const BlockStorage &other) const;
    bool operator!=(const BlockStorage &other) const;

    void reserve(void *user_context, size_t capacity, bool free_existing = false);
    void resize(void *user_context, size_t entry_count, bool realloc = true);

    void assign(void *user_context, size_t index, const void *entry_ptr);
    void insert(void *user_context, size_t index, const void *entry_ptr);
    void prepend(void *user_context, const void *entry_ptr);
    void append(void *user_context, const void *entry_ptr);
    void remove(void *user_context, size_t index);

    void fill(void *user_context, const void *array, size_t array_size);
    void insert(void *user_context, size_t index, const void *array, size_t array_size);
    void replace(void *user_context, size_t index, const void *array, size_t array_size);
    void prepend(void *user_context, const void *array, size_t array_size);
    void append(void *user_context, const void *array, size_t array_size);
    void remove(void *user_context, size_t index, size_t entry_count);

    void pop_front(void *user_context);
    void pop_back(void *user_context);
    void shrink_to_fit(void *user_context);
    void clear(void *user_context);
    void destroy(void *user_context);

    bool empty() const;
    size_t stride() const;
    size_t size() const;

    void *operator[](size_t index);  ///< logical entry index (returns ptr = data() + (index * stride())
    const void *operator[](size_t index) const;

    void *data();
    void *front();
    void *back();

    const void *data() const;
    const void *front() const;
    const void *back() const;

    const Config &current_config() const;
    static const Config &default_config();

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    void allocate(void *user_context, size_t capacity);

    void *ptr = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    Config config;
    SystemMemoryAllocatorFns allocator;
};

BlockStorage::BlockStorage(void *user_context, const Config &cfg, const SystemMemoryAllocatorFns &sma)
    : config(cfg), allocator(sma) {
    halide_debug_assert(user_context, config.entry_size != 0);
    halide_debug_assert(user_context, allocator.allocate != nullptr);
    halide_debug_assert(user_context, allocator.deallocate != nullptr);
    if (config.minimum_capacity) {
        reserve(user_context, config.minimum_capacity);
    }
}

BlockStorage::BlockStorage(const BlockStorage &other)
    : BlockStorage(nullptr, other.config, other.allocator) {
    if (other.count) {
        resize(nullptr, other.count);
        memcpy(this->ptr, other.ptr, count * config.entry_size);
    }
}

BlockStorage::~BlockStorage() {
    destroy(nullptr);
}

void BlockStorage::destroy(void *user_context) {
    halide_debug_assert(user_context, allocator.deallocate != nullptr);
    if (ptr != nullptr) {
        allocator.deallocate(user_context, ptr);
    }
    capacity = count = 0;
    ptr = nullptr;
}

void BlockStorage::initialize(void *user_context, const Config &cfg, const SystemMemoryAllocatorFns &sma) {
    allocator = sma;
    config = cfg;
    capacity = count = 0;
    ptr = nullptr;
    if (config.minimum_capacity) {
        reserve(user_context, config.minimum_capacity);
    }
}

BlockStorage &BlockStorage::operator=(const BlockStorage &other) {
    if (&other != this) {
        config = other.config;
        resize(nullptr, other.count);
        if (count != 0 && other.ptr != nullptr) {
            memcpy(ptr, other.ptr, count * config.entry_size);
        }
    }
    return *this;
}

bool BlockStorage::operator==(const BlockStorage &other) const {
    if (config.entry_size != other.config.entry_size) {
        return false;
    }
    if (count != other.count) {
        return false;
    }
    return memcmp(this->ptr, other.ptr, this->size() * config.entry_size) == 0;
}

bool BlockStorage::operator!=(const BlockStorage &other) const {
    return !(*this == other);
}

void BlockStorage::fill(void *user_context, const void *array, size_t array_size) {
    if (array_size != 0) {
        resize(user_context, array_size);
        memcpy(this->ptr, array, array_size * config.entry_size);
        count = array_size;
    }
}

void BlockStorage::assign(void *user_context, size_t index, const void *entry_ptr) {
    replace(user_context, index, entry_ptr, 1);
}

void BlockStorage::prepend(void *user_context, const void *entry_ptr) {
    insert(user_context, 0, entry_ptr, 1);
}

void BlockStorage::append(void *user_context, const void *entry_ptr) {
    append(user_context, entry_ptr, 1);
}

void BlockStorage::pop_front(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    remove(user_context, 0);
}

void BlockStorage::pop_back(void *user_context) {
    halide_debug_assert(user_context, count > 0);
    resize(user_context, size() - 1);
}

void BlockStorage::clear(void *user_context) {
    resize(user_context, 0);
}

void BlockStorage::reserve(void *user_context, size_t new_capacity, bool free_existing) {
    new_capacity = max(new_capacity, count);

    if ((new_capacity < capacity) && !free_existing) {
        new_capacity = capacity;
    }

    allocate(user_context, new_capacity);
}

void BlockStorage::resize(void *user_context, size_t entry_count, bool realloc) {
    size_t current_size = capacity;
    size_t requested_size = entry_count;
    size_t minimum_size = config.minimum_capacity;
    size_t actual_size = current_size;
    count = requested_size;

    // increase capacity upto 1.5x existing (or at least min_capacity)
    if (requested_size > current_size) {
        actual_size = max(requested_size, max(current_size * 3 / 2, minimum_size));
    } else if (!realloc) {
        return;
    }

#if DEBUG
    debug(user_context) << "BlockStorage: Resize ("
                        << "requested_size=" << (int32_t)requested_size << " "
                        << "current_size=" << (int32_t)current_size << " "
                        << "minimum_size=" << (int32_t)minimum_size << " "
                        << "actual_size=" << (int32_t)actual_size << " "
                        << "entry_size=" << (int32_t)config.entry_size << " "
                        << "realloc=" << (realloc ? "true" : "false") << ")...\n";
#endif

    allocate(user_context, actual_size);
}

void BlockStorage::shrink_to_fit(void *user_context) {
    if (capacity > count) {
        void *new_ptr = nullptr;
        if (count > 0) {
            size_t actual_bytes = count * config.entry_size;
            new_ptr = allocator.allocate(user_context, actual_bytes);
            memcpy(new_ptr, ptr, actual_bytes);
        }
        allocator.deallocate(user_context, ptr);
        capacity = count;
        ptr = new_ptr;
    }
}

void BlockStorage::insert(void *user_context, size_t index, const void *entry_ptr) {
    insert(user_context, index, entry_ptr, 1);
}

void BlockStorage::remove(void *user_context, size_t index) {
    remove(user_context, index, 1);
}

void BlockStorage::remove(void *user_context, size_t index, size_t entry_count) {
    halide_debug_assert(user_context, index < count);
    const size_t last_index = size();
    if (index < (last_index - entry_count)) {
        size_t dst_offset = index * config.entry_size;
        size_t src_offset = (index + entry_count) * config.entry_size;
        size_t bytes = (last_index - index - entry_count) * config.entry_size;

#if DEBUG
        debug(0) << "BlockStorage: Remove ("
                 << "index=" << (int32_t)index << " "
                 << "entry_count=" << (int32_t)entry_count << " "
                 << "entry_size=" << (int32_t)config.entry_size << " "
                 << "last_index=" << (int32_t)last_index << " "
                 << "src_offset=" << (int32_t)src_offset << " "
                 << "dst_offset=" << (int32_t)dst_offset << " "
                 << "bytes=" << (int32_t)bytes << ")...\n";
#endif
        void *dst_ptr = offset_address(ptr, dst_offset);
        void *src_ptr = offset_address(ptr, src_offset);
        memmove(dst_ptr, src_ptr, bytes);
    }
    resize(user_context, last_index - entry_count);
}

void BlockStorage::replace(void *user_context, size_t index, const void *array, size_t array_size) {
    halide_debug_assert(user_context, index < count);
    size_t offset = index * config.entry_size;
    size_t remaining = count - index;

#if DEBUG
    debug(0) << "BlockStorage: Replace ("
             << "index=" << (int32_t)index << " "
             << "array_size=" << (int32_t)array_size << " "
             << "entry_size=" << (int32_t)config.entry_size << " "
             << "offset=" << (int32_t)offset << " "
             << "remaining=" << (int32_t)remaining << " "
             << "capacity=" << (int32_t)capacity << ")...\n";
#endif

    halide_debug_assert(user_context, remaining > 0);
    size_t copy_count = min(remaining, array_size);
    void *dst_ptr = offset_address(ptr, offset);
    memcpy(dst_ptr, array, copy_count * config.entry_size);
    count = max(count, index + copy_count);
}

void BlockStorage::insert(void *user_context, size_t index, const void *array, size_t array_size) {
    halide_debug_assert(user_context, index <= count);
    const size_t last_index = size();
    resize(user_context, last_index + array_size);
    if (index < last_index) {
        size_t src_offset = index * config.entry_size;
        size_t dst_offset = (index + array_size) * config.entry_size;
        size_t bytes = (last_index - index) * config.entry_size;
        void *src_ptr = offset_address(ptr, src_offset);
        void *dst_ptr = offset_address(ptr, dst_offset);
        memmove(dst_ptr, src_ptr, bytes);
    }
    replace(user_context, index, array, array_size);
}

void BlockStorage::prepend(void *user_context, const void *array, size_t array_size) {
    insert(user_context, 0, array, array_size);
}

void BlockStorage::append(void *user_context, const void *array, size_t array_size) {
    const size_t last_index = size();
    insert(user_context, last_index, array, array_size);
}

bool BlockStorage::empty() const {
    return count == 0;
}

size_t BlockStorage::size() const {
    return count;
}

size_t BlockStorage::stride() const {
    return config.entry_size;
}

void *BlockStorage::operator[](size_t index) {
    halide_debug_assert(nullptr, index < capacity);
    return offset_address(ptr, index * config.entry_size);
}

const void *BlockStorage::operator[](size_t index) const {
    halide_debug_assert(nullptr, index < capacity);
    return offset_address(ptr, index * config.entry_size);
}

void *BlockStorage::data() {
    return ptr;
}

void *BlockStorage::front() {
    halide_debug_assert(nullptr, count > 0);
    return ptr;
}

void *BlockStorage::back() {
    halide_debug_assert(nullptr, count > 0);
    size_t index = count - 1;
    return offset_address(ptr, index * config.entry_size);
}

const void *BlockStorage::data() const {
    return ptr;
}

const void *BlockStorage::front() const {
    halide_debug_assert(nullptr, count > 0);
    return ptr;
}

const void *BlockStorage::back() const {
    halide_debug_assert(nullptr, count > 0);
    size_t index = count - 1;
    return offset_address(ptr, index * config.entry_size);
}

void BlockStorage::allocate(void *user_context, size_t new_capacity) {
    if (new_capacity != capacity) {
        halide_debug_assert(user_context, allocator.allocate != nullptr);
        size_t requested_bytes = new_capacity * config.entry_size;
        size_t block_size = max(config.block_size, config.entry_size);
        size_t block_count = (requested_bytes / block_size);
        block_count += (requested_bytes % block_size) ? 1 : 0;
        size_t alloc_size = block_count * block_size;
#if DEBUG
        debug(0) << "BlockStorage: Allocating ("
                 << "requested_bytes=" << (int32_t)requested_bytes << " "
                 << "block_size=" << (int32_t)block_size << " "
                 << "block_count=" << (int32_t)block_count << " "
                 << "alloc_size=" << (int32_t)alloc_size << ") ...\n";
#endif
        void *new_ptr = alloc_size ? allocator.allocate(user_context, alloc_size) : nullptr;
        if (count != 0 && ptr != nullptr && new_ptr != nullptr) {
            memcpy(new_ptr, ptr, count * config.entry_size);
        }
        if (ptr != nullptr) {
            halide_debug_assert(user_context, allocator.deallocate != nullptr);
            allocator.deallocate(user_context, ptr);
        }
        capacity = new_capacity;
        ptr = new_ptr;
    }
}

const SystemMemoryAllocatorFns &
BlockStorage::current_allocator() const {
    return this->allocator;
}

const BlockStorage::Config &
BlockStorage::default_config() {
    static Config default_cfg;
    return default_cfg;
}

const BlockStorage::Config &
BlockStorage::current_config() const {
    return this->config;
}

const SystemMemoryAllocatorFns &
BlockStorage::default_allocator() {
    static SystemMemoryAllocatorFns native_allocator = {
        native_system_malloc, native_system_free};
    return native_allocator;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_STORAGE_H
