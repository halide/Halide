#ifndef HALIDE_RUNTIME_HASHMAP_H
#define HALIDE_RUNTIME_HASHMAP_H

// the bulk of this hashmap implementation is based on 'cache.cpp'

#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide { namespace Runtime { namespace Internal {

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

typedef void(*copy_value_func)(uint8_t *dst, const uint8_t *src, size_t size);
typedef void(*destroy_value_func)(uint8_t *value, size_t size);

struct CacheEntry {
    CacheEntry *next;
    CacheEntry *more_recent;
    CacheEntry *less_recent;
    uint8_t *metadata_storage;
    size_t key_size;
    uint8_t *key;
    uint32_t hash;
    uint32_t in_use_count; // 0 if none returned from halide_cache_lookup

    // The actual stored data.
    size_t   value_size;
    uint8_t* value;

    bool init(const uint8_t *cache_key, size_t cache_key_size,
              uint32_t key_hash,
              const uint8_t* cache_value, size_t cache_value_size,
              copy_value_func copy_value);
    void destroy(destroy_value_func destroy_value);
};

inline bool CacheEntry::init(const uint8_t *cache_key, size_t cache_key_size,
                           uint32_t key_hash,
                           const uint8_t* cache_value, size_t cache_value_size,
                           copy_value_func copy_value) {
    next = NULL;
    more_recent = NULL;
    less_recent = NULL;
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
    storage_bytes /= alignment;         // positive integer division (floor)
    storage_bytes *= alignment;

    // Then storage for the key, starting immediately after the (aligned) value:
    const size_t key_offset = storage_bytes;
    storage_bytes += key_size;

    // Do the single malloc call
    metadata_storage = (uint8_t *)halide_malloc(NULL, storage_bytes);
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

inline void CacheEntry::destroy(destroy_value_func destroy_value) {
    destroy_value(value, value_size);
    halide_free(NULL, metadata_storage);
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

    bool inited;

    bool init(copy_value_func copy_value, destroy_value_func destroy_value);
    void prune();
    void set_size(int64_t size);
    int lookup(void *user_context, const uint8_t *cache_key, int32_t size, uint8_t *cache_value, size_t cache_value_size);
    int store (void *user_context, const uint8_t *cache_key, int32_t size, const uint8_t *cache_value, size_t cache_value_size);
    void release(void *user_context, void *host);
    void cleanup();
};

inline bool HashMap::init(copy_value_func _copy_value, destroy_value_func _destroy_value) {
    memset(&memoization_lock, 0, sizeof(halide_mutex));
    halide_assert(NULL, !inited);
    most_recently_used  = NULL;
    least_recently_used = NULL;
    kDefaultCacheSize   = 1 << 20;
    max_cache_size      = kDefaultCacheSize;
    current_cache_size  = 0;
    for (size_t i = 0; i < kHashTableSize; ++i) {
        cache_entries[i] = NULL;
    }
    halide_assert(NULL, _copy_value);
    halide_assert(NULL, _destroy_value);
    this->copy_value = _copy_value;
    this->destroy_value = _destroy_value;
    inited = true;
    return true;
}

inline void HashMap::prune() {
#if CACHE_DEBUGGING
    validate_cache();
#endif
    CacheEntry *prune_candidate = least_recently_used;
    while (current_cache_size > max_cache_size &&
           prune_candidate != NULL) {
        CacheEntry *more_recent = prune_candidate->more_recent;

        if (prune_candidate->in_use_count == 0) {
            uint32_t h = prune_candidate->hash;
            uint32_t index = h % kHashTableSize;

            // Remove from hash table
            CacheEntry *prev_hash_entry = cache_entries[index];
            if (prev_hash_entry == prune_candidate) {
                cache_entries[index] = prune_candidate->next;
            } else {
                while (prev_hash_entry != NULL && prev_hash_entry->next != prune_candidate) {
                    prev_hash_entry = prev_hash_entry->next;
                }
                halide_assert(NULL, prev_hash_entry != NULL);
                prev_hash_entry->next = prune_candidate->next;
            }

            // Remove from less recent chain.
            if (least_recently_used == prune_candidate) {
                least_recently_used = more_recent;
            }
            if (more_recent != NULL) {
                more_recent->less_recent = prune_candidate->less_recent;
            }

            // Remove from more recent chain.
            if (most_recently_used == prune_candidate) {
                most_recently_used = prune_candidate->less_recent;
            }
            if (prune_candidate->less_recent != NULL) {
                prune_candidate->less_recent = more_recent;
            }

            // Decrease cache used amount.
            current_cache_size -= prune_candidate->value_size;

            // Deallocate the entry.
            prune_candidate->destroy(destroy_value);
            halide_free(NULL, prune_candidate);
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
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size)) {

                if (entry != most_recently_used) {
                    halide_assert(user_context, entry->more_recent != NULL);
                    if (entry->less_recent != NULL) {
                        entry->less_recent->more_recent = entry->more_recent;
                    } else {
                        halide_assert(user_context, least_recently_used == entry);
                        least_recently_used = entry->more_recent;
                    }
                    halide_assert(user_context, entry->more_recent != NULL);
                    entry->more_recent->less_recent = entry->less_recent;

                    entry->more_recent = NULL;
                    entry->less_recent = most_recently_used;
                    if (most_recently_used != NULL) {
                        most_recently_used->more_recent = entry;
                    }
                    most_recently_used = entry;
                }

                halide_assert(user_context, (cache_value_size == entry->value_size))
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
         entry != NULL;
         entry = entry->next) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size)) {
                halide_assert(user_context, (cache_value_size == entry->value_size));
                destroy_value(entry->value, entry->value_size);
                copy_value(entry->value, cache_value, entry->value_size);
                return(0);
        }
    }

    // key not found: create new entry
    CacheEntry *new_entry = (CacheEntry*)halide_malloc(NULL, sizeof(CacheEntry));
    bool inited = new_entry->init(cache_key, size, h, cache_value, cache_value_size, copy_value);
    halide_assert(user_context, inited);

    uint64_t added_size = cache_value_size;
    current_cache_size += added_size;
    prune();

    new_entry->next = cache_entries[index];
    new_entry->less_recent = most_recently_used;
    if (most_recently_used != NULL) {
        most_recently_used->more_recent = new_entry;
    }
    most_recently_used = new_entry;
    if (least_recently_used == NULL) {
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
    halide_assert(user_context, false);
    debug(user_context) << "Exited halide_memoization_cache_release.\n";
}

inline void HashMap::cleanup() {
    debug(NULL) << "halide_memoization_cache_cleanup\n";
    for (size_t i = 0; i < kHashTableSize; i++) {
        CacheEntry *entry = cache_entries[i];
        cache_entries[i] = NULL;
        while (entry != NULL) {
            CacheEntry *next = entry->next;
            entry->destroy(destroy_value);
            halide_free(NULL, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    most_recently_used = NULL;
    least_recently_used = NULL;
}


// THashMap: a convenience class for using HashMap with actual types
template<typename KeyType, typename ValueType>
struct THashMap : public HashMap {

    // TODO(marcos): perhaps use KeyType for something useful...
    // with some generalized interface for keys, we should be able to get rid of
    // "const uint8_t *cache_key, int32_t key_size" in the 'lookup' and 'store'
    // member functions below...

    static void copy_value_func(uint8_t *dst, const uint8_t *src, size_t size) {
        halide_assert(NULL, sizeof(ValueType) == size);
        ValueType *D = reinterpret_cast<ValueType*>(dst);
        const ValueType *S = reinterpret_cast<const ValueType*>(src);
        *D = *S;
    }

    static void destroy_value_func(uint8_t *value, size_t size) {
        halide_assert(NULL, sizeof(ValueType) == size);
        ValueType *V = reinterpret_cast<ValueType*>(value);
        V->~ValueType();
    }

    bool init() {
        return HashMap::init(copy_value_func, destroy_value_func);
    }

    int lookup(void *user_context, const uint8_t *cache_key, int32_t key_size, ValueType *cache_value) {
        return HashMap::lookup(user_context, cache_key, key_size, (uint8_t*)cache_value, sizeof(ValueType));
    }

    int store(void *user_context, const uint8_t *cache_key, int32_t key_size, const ValueType *cache_value) {
        return HashMap::store(user_context, cache_key, key_size, (const uint8_t*)cache_value, sizeof(ValueType));
    }

};

}}}

#endif//HALIDE_RUNTIME_HASHMAP_H
