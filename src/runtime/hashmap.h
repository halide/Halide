#ifndef HALIDE_RUNTIME_HASHMAP_H
#define HALIDE_RUNTIME_HASHMAP_H

// the bulk of this hashmap implementation is based on 'cache.cpp'

#include "printer.h"
#include "scoped_mutex_lock.h"

// By default, hashmap_malloc() and hashmap_free() simply wrap around
// halide_malloc() and halide_free(), respectively. It is possible to
// override the implementation by providing the corresponding #define
// prior to including "hashmap.h":
//
#ifndef hashmap_malloc
#define hashmap_malloc(user_context, size) halide_malloc(user_context, size)
#endif  // hashmap_malloc
//
#ifndef hashmap_free
#define hashmap_free(user_context, memory) halide_free(user_context, memory)
#endif  // hashmap_free

namespace Halide {
namespace Runtime {
namespace Internal {

inline bool keys_equal(const uint8_t *key1, const uint8_t *key2, size_t key_size) {
    return memcmp(key1, key2, key_size) == 0;
}

inline uint32_t djb_hash(const uint8_t *key, size_t key_size) {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
        h = (h << 5) + h + key[i];
    }
    return h;
}

typedef void (*copy_value_func)(uint8_t *dst, const uint8_t *src, size_t size);
typedef void (*destroy_value_func)(uint8_t *value, size_t size);

struct CacheEntry {
    CacheEntry *next;
    CacheEntry *more_recent;
    CacheEntry *less_recent;
    uint8_t *metadata_storage;
    size_t key_size;
    uint8_t *key;
    uint32_t hash;
    uint32_t in_use_count;  // 0 if none returned from halide_cache_lookup

    // The actual stored data.
    size_t value_size;
    uint8_t *value;

    bool init(void *user_context,
              const uint8_t *cache_key, size_t cache_key_size,
              uint32_t key_hash,
              const uint8_t *cache_value, size_t cache_value_size,
              copy_value_func copy_value);
    void destroy(void *user_context,
                 destroy_value_func destroy_value);
};

inline bool CacheEntry::init(void *user_context,
                             const uint8_t *cache_key, size_t cache_key_size,
                             uint32_t key_hash,
                             const uint8_t *cache_value, size_t cache_value_size,
                             copy_value_func copy_value) {
    next = nullptr;
    more_recent = nullptr;
    less_recent = nullptr;
    key_size = cache_key_size;
    hash = key_hash;
    in_use_count = 0;

    // Allocate all the necessary space (or die)
    size_t storage_bytes = 0;

    // First storage for value:
    storage_bytes += cache_value_size;

    // Enforce some alignment between value and key:
    const size_t alignment = 8;
    storage_bytes += (alignment - 1);
    storage_bytes /= alignment;  // positive integer division (floor)
    storage_bytes *= alignment;

    // Then storage for the key, starting immediately after the (aligned) value:
    const size_t key_offset = storage_bytes;
    storage_bytes += key_size;

    // Do the single malloc call
    metadata_storage = (uint8_t *)hashmap_malloc(user_context, storage_bytes);
    if (!metadata_storage) {
        return false;
    }

    // Set up the pointers into the allocated metadata space
    value = metadata_storage;
    key = metadata_storage + key_offset;

    // Copy over the key
    for (size_t i = 0; i < key_size; i++) {
        key[i] = cache_key[i];
    }

    // Copy the value:
    copy_value(value, cache_value, cache_value_size);
    value_size = cache_value_size;

    return true;
}

inline void CacheEntry::destroy(void *user_context,
                                destroy_value_func destroy_value) {
    destroy_value(value, value_size);
    hashmap_free(user_context, metadata_storage);
}

struct HashMap {
    halide_mutex memoization_lock;

    static const size_t kHashTableSize = 256;

    CacheEntry *cache_entries[kHashTableSize];

    CacheEntry *most_recently_used;
    CacheEntry *least_recently_used;

    uint64_t kDefaultCacheSize;
    int64_t max_cache_size;
    int64_t current_cache_size;

    copy_value_func copy_value;
    destroy_value_func destroy_value;

    void *user_context;

    bool inited;

    bool init(void *user_context, copy_value_func copy_value, destroy_value_func destroy_value);
    void prune();
    void set_size(int64_t size);
    int lookup(void *user_context, const uint8_t *cache_key, int32_t size, uint8_t *cache_value, size_t cache_value_size);
    int store(void *user_context, const uint8_t *cache_key, int32_t size, const uint8_t *cache_value, size_t cache_value_size);
    void release(void *user_context, void *host);
    void cleanup();
};

inline bool HashMap::init(void *user_context, copy_value_func _copy_value, destroy_value_func _destroy_value) {
    memset(&memoization_lock, 0, sizeof(halide_mutex));
    halide_debug_assert(nullptr, !inited);
    most_recently_used = nullptr;
    least_recently_used = nullptr;
    kDefaultCacheSize = 1 << 20;
    max_cache_size = kDefaultCacheSize;
    current_cache_size = 0;
    for (auto &cache_entry_ref : cache_entries) {
        cache_entry_ref = nullptr;
    }
    halide_debug_assert(nullptr, _copy_value);
    halide_debug_assert(nullptr, _destroy_value);
    this->copy_value = _copy_value;
    this->destroy_value = _destroy_value;
    inited = true;
    this->user_context = user_context;
    return true;
}

inline void HashMap::prune() {
#if CACHE_DEBUGGING
    validate_cache();
#endif
    CacheEntry *prune_candidate = least_recently_used;
    while (current_cache_size > max_cache_size &&
           prune_candidate != nullptr) {
        CacheEntry *more_recent = prune_candidate->more_recent;

        if (prune_candidate->in_use_count == 0) {
            uint32_t h = prune_candidate->hash;
            uint32_t index = h % kHashTableSize;

            // Remove from hash table
            CacheEntry *prev_hash_entry = cache_entries[index];
            if (prev_hash_entry == prune_candidate) {
                cache_entries[index] = prune_candidate->next;
            } else {
                while (prev_hash_entry != nullptr && prev_hash_entry->next != prune_candidate) {
                    prev_hash_entry = prev_hash_entry->next;
                }
                halide_debug_assert(nullptr, prev_hash_entry != nullptr);
                prev_hash_entry->next = prune_candidate->next;
            }

            // Remove from less recent chain.
            if (least_recently_used == prune_candidate) {
                least_recently_used = more_recent;
            }
            if (more_recent != nullptr) {
                more_recent->less_recent = prune_candidate->less_recent;
            }

            // Remove from more recent chain.
            if (most_recently_used == prune_candidate) {
                most_recently_used = prune_candidate->less_recent;
            }
            if (prune_candidate->less_recent != nullptr) {
                prune_candidate->less_recent = more_recent;
            }

            // Decrease cache used amount.
            current_cache_size -= prune_candidate->value_size;

            // Deallocate the entry.
            prune_candidate->destroy(this->user_context, destroy_value);
            hashmap_free(this->user_context, prune_candidate);
        }

        prune_candidate = more_recent;
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

inline void HashMap::set_size(int64_t size) {
    if (size == 0) {
        size = kDefaultCacheSize;
    }

    ScopedMutexLock lock(&memoization_lock);

    max_cache_size = size;
    prune();
}

inline int HashMap::lookup(void *user_context,
                           const uint8_t *cache_key, int32_t size,
                           uint8_t *cache_value, size_t cache_value_size) {
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedMutexLock lock(&memoization_lock);

#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_lookup", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        for (int32_t i = 0; i < tuple_count; i++) {
            halide_buffer_t *buf = tuple_buffers[i];
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
    }
#endif

    CacheEntry *entry = cache_entries[index];
    while (entry != nullptr) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size)) {

            if (entry != most_recently_used) {
                halide_debug_assert(user_context, entry->more_recent != nullptr);
                if (entry->less_recent != nullptr) {
                    entry->less_recent->more_recent = entry->more_recent;
                } else {
                    halide_debug_assert(user_context, least_recently_used == entry);
                    least_recently_used = entry->more_recent;
                }
                halide_debug_assert(user_context, entry->more_recent != nullptr);
                entry->more_recent->less_recent = entry->less_recent;

                entry->more_recent = nullptr;
                entry->less_recent = most_recently_used;
                if (most_recently_used != nullptr) {
                    most_recently_used->more_recent = entry;
                }
                most_recently_used = entry;
            }

            halide_debug_assert(user_context, (cache_value_size == entry->value_size));
            copy_value(cache_value, entry->value, entry->value_size);

            entry->in_use_count += 1;

            return 0;
        }
        entry = entry->next;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif

    return 1;
}

inline int HashMap::store(void *user_context,
                          const uint8_t *cache_key, int32_t size,
                          const uint8_t *cache_value, size_t cache_value_size) {
    debug(user_context) << "halide_memoization_cache_store\n";

    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedMutexLock lock(&memoization_lock);

#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_store", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        for (int32_t i = 0; i < tuple_count; i++) {
            halide_buffer_t *buf = tuple_buffers[i];
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
    }
#endif

    // key is already present in the hashmap: overwrite value
    for (CacheEntry *entry = cache_entries[index];
         entry != nullptr;
         entry = entry->next) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size)) {
            halide_debug_assert(user_context, (cache_value_size == entry->value_size));
            destroy_value(entry->value, entry->value_size);
            copy_value(entry->value, cache_value, entry->value_size);
            return (0);
        }
    }

    // key not found: create new entry
    CacheEntry *new_entry = (CacheEntry *)hashmap_malloc(user_context, sizeof(CacheEntry));
    bool inited = new_entry->init(user_context, cache_key, size, h, cache_value, cache_value_size, copy_value);
    halide_debug_assert(user_context, inited);
    (void)inited;

    uint64_t added_size = cache_value_size;
    current_cache_size += added_size;
    prune();

    new_entry->next = cache_entries[index];
    new_entry->less_recent = most_recently_used;
    if (most_recently_used != nullptr) {
        most_recently_used->more_recent = new_entry;
    }
    most_recently_used = new_entry;
    if (least_recently_used == nullptr) {
        least_recently_used = new_entry;
    }
    cache_entries[index] = new_entry;

    new_entry->in_use_count = 1;

#if CACHE_DEBUGGING
    validate_cache();
#endif
    debug(user_context) << "Exiting halide_memoization_cache_store\n";

    return 0;
}

inline void HashMap::release(void *user_context, void *host) {
    debug(user_context) << "halide_memoization_cache_release\n";
    // TODO(marcos): this method does not make sense on a generic hashmap... remove it?
    halide_debug_assert(user_context, false);
    debug(user_context) << "Exited halide_memoization_cache_release.\n";
}

inline void HashMap::cleanup() {
    debug(nullptr) << "halide_memoization_cache_cleanup\n";
    for (auto &cache_entry_ref : cache_entries) {
        CacheEntry *entry = cache_entry_ref;
        cache_entry_ref = nullptr;
        while (entry != nullptr) {
            CacheEntry *next = entry->next;
            entry->destroy(this->user_context, destroy_value);
            hashmap_free(this->user_context, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    most_recently_used = nullptr;
    least_recently_used = nullptr;
}

// THashMap: a convenience class for using HashMap with actual types
template<typename KeyType, typename ValueType>
struct THashMap : public HashMap {

    // TODO(marcos): perhaps use KeyType for something useful...
    // with some generalized interface for keys, we should be able to get rid of
    // "const uint8_t *cache_key, int32_t key_size" in the 'lookup' and 'store'
    // member functions below...

    static void copy_value_func(uint8_t *dst, const uint8_t *src, size_t size) {
        halide_debug_assert(nullptr, sizeof(ValueType) == size);
        ValueType *D = reinterpret_cast<ValueType *>(dst);
        const ValueType *S = reinterpret_cast<const ValueType *>(src);
        *D = *S;
    }

    static void destroy_value_func(uint8_t *value, size_t size) {
        halide_debug_assert(nullptr, sizeof(ValueType) == size);
        ValueType *V = reinterpret_cast<ValueType *>(value);
        V->~ValueType();
    }

    bool init(void *user_context) {
        return HashMap::init(user_context, copy_value_func, destroy_value_func);
    }

    int lookup(void *user_context, const uint8_t *cache_key, int32_t key_size, ValueType *cache_value) {
        return HashMap::lookup(user_context, cache_key, key_size, (uint8_t *)cache_value, sizeof(ValueType));
    }

    int store(void *user_context, const uint8_t *cache_key, int32_t key_size, const ValueType *cache_value) {
        return HashMap::store(user_context, cache_key, key_size, (const uint8_t *)cache_value, sizeof(ValueType));
    }
};

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_HASHMAP_H
