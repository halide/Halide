#ifndef HALIDE_RUNTIME_HASHMAP_H
#define HALIDE_RUNTIME_HASHMAP_H

// the bulk of this hashmap implementation is based on 'cache.cpp'

#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK bool keys_equal(const uint8_t *key1, const uint8_t *key2, size_t key_size) {
    return memcmp(key1, key2, key_size) == 0;
}

template<typename ValueType>
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
    ValueType* value;

    bool init(const uint8_t *cache_key, size_t cache_key_size,
              uint32_t key_hash, ValueType* value);
    void destroy();
};

template<typename ValueType>
struct CacheBlockHeader {
    CacheEntry<ValueType> *entry;
    uint32_t hash;
};

// Each host block has extra space to store a header just before the
// contents. This block must respect the same alignment as
// halide_malloc, because it offsets the return value from
// halide_malloc. The header holds the cache key hash and pointer to
// the hash entry.
template<typename ValueType>
WEAK __attribute((always_inline)) size_t header_bytes() {
    size_t s = sizeof(CacheBlockHeader<ValueType>);
    size_t mask = halide_malloc_alignment() - 1;
    return (s + mask) & ~mask;
}

template<typename ValueType>
WEAK CacheBlockHeader<ValueType> *get_pointer_to_header(uint8_t * host) {
    return (CacheBlockHeader<ValueType> *)(host - header_bytes<ValueType>());
}

template<typename ValueType>
WEAK bool CacheEntry<ValueType>::init(const uint8_t *cache_key, size_t cache_key_size,
                                      uint32_t key_hash, ValueType* cache_value) {
    next = NULL;
    more_recent = NULL;
    less_recent = NULL;
    key_size = cache_key_size;
    hash = key_hash;
    in_use_count = 0;

    // Allocate all the necessary space (or die)
    size_t storage_bytes = 0;

    // First storage for value:
    storage_bytes += sizeof(ValueType);

    // NOTE(marcos): maybe enforce some alignment between value and key?
    //storage_bytes = (storage_bytes + 7) / 8;

    // Then storage for the key:
    size_t key_offset = storage_bytes;
    storage_bytes += key_size;

    // Do the single malloc call
    metadata_storage = (uint8_t *)halide_malloc(NULL, storage_bytes);
    if (!metadata_storage) {
        return false;
    }

    // Set up the pointers into the allocated metadata space
    value = (ValueType*)metadata_storage;
    key = metadata_storage + key_offset;

    // Copy over the key
    for (size_t i = 0; i < key_size; i++) {
        key[i] = cache_key[i];
    }

    // Copy the value:
    *value = *cache_value;

    return true;
}

template<typename ValueType>
WEAK void CacheEntry<ValueType>::destroy() {
    value->~ValueType();
    halide_free(NULL, metadata_storage);
}

WEAK uint32_t djb_hash(const uint8_t *key, size_t key_size)  {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
      h = (h << 5) + h + key[i];
    }
    return h;
}

template<typename KeyType, typename ValueType>
struct HashMap
{
    halide_mutex memoization_lock;

    static const size_t kHashTableSize = 256;

    CacheEntry<ValueType> *cache_entries[kHashTableSize];

    CacheEntry<ValueType> *most_recently_used;
    CacheEntry<ValueType> *least_recently_used;

    uint64_t kDefaultCacheSize;
    int64_t max_cache_size;
    int64_t current_cache_size;

    bool inited;

    bool init()
    {
        memset(&memoization_lock, 0, sizeof(halide_mutex));
        halide_assert(NULL, !inited);
        most_recently_used  = NULL;
        least_recently_used = NULL;
        kDefaultCacheSize   = 1 << 20;
        max_cache_size      = kDefaultCacheSize;
        current_cache_size  = 0;
        for (int i = 0; i < kHashTableSize; ++i) {
            cache_entries[i] = NULL;
        }
        inited = true;
        return true;
    }

    void prune();
    void set_size(int64_t size);
    int lookup(void *user_context, const uint8_t *cache_key, int32_t size, ValueType* cache_value);
    int store (void *user_context, const uint8_t *cache_key, int32_t size, ValueType* cache_value);
    void release(void *user_context, void *host);
    void cleanup();
};

template<typename KeyType, typename ValueType>
WEAK void HashMap<KeyType, ValueType>::prune() {
#if CACHE_DEBUGGING
    validate_cache();
#endif
    CacheEntry<ValueType> *prune_candidate = least_recently_used;
    while (current_cache_size > max_cache_size &&
           prune_candidate != NULL) {
        CacheEntry<ValueType> *more_recent = prune_candidate->more_recent;

        if (prune_candidate->in_use_count == 0) {
            uint32_t h = prune_candidate->hash;
            uint32_t index = h % kHashTableSize;

            // Remove from hash table
            CacheEntry<ValueType> *prev_hash_entry = cache_entries[index];
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
            current_cache_size -= sizeof(ValueType);

            // Deallocate the entry.
            prune_candidate->destroy();
            halide_free(NULL, prune_candidate);
        }

        prune_candidate = more_recent;
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

template<typename KeyType, typename ValueType>
WEAK void HashMap<KeyType, ValueType>::set_size(int64_t size) {
    if (size == 0) {
        size = kDefaultCacheSize;
    }

    ScopedMutexLock lock(&memoization_lock);

    max_cache_size = size;
    prune();
}

template<typename KeyType, typename ValueType>
WEAK int HashMap<KeyType, ValueType>::lookup(void *user_context,
                                             const uint8_t *cache_key, int32_t size,
                                             ValueType* cache_value) {
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

    CacheEntry<ValueType> *entry = cache_entries[index];
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

                *cache_value = *entry->value;

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

template<typename KeyType, typename ValueType>
WEAK int HashMap<KeyType, ValueType>::store(void *user_context,
                                            const uint8_t *cache_key, int32_t size,
                                            ValueType* cache_value) {
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
    for (CacheEntry<ValueType> *entry = cache_entries[index];
         entry != NULL;
         entry = entry->next) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size)) {
                // TODO(marcos): release old value
                entry->value = cache_value;
                return(0);
        }
    }

    // key not found: create new entry
    CacheEntry<ValueType> *new_entry = (CacheEntry<ValueType>*)halide_malloc(NULL, sizeof(CacheEntry<ValueType>));
    bool inited = new_entry->init(cache_key, size, h, cache_value);
    halide_assert(user_context, inited);

    uint64_t added_size = sizeof(ValueType);
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

template<typename KeyType, typename ValueType>
WEAK void HashMap<KeyType, ValueType>::release(void *user_context, void *host) {
    CacheBlockHeader<ValueType> *header = get_pointer_to_header<ValueType>((uint8_t *)host);
    debug(user_context) << "halide_memoization_cache_release\n";
    CacheEntry<ValueType> *entry = header->entry;

    if (entry == NULL) {
        halide_free(user_context, header);
    } else {
        ScopedMutexLock lock(&memoization_lock);

        halide_assert(user_context, entry->in_use_count > 0);
        entry->in_use_count--;
#if CACHE_DEBUGGING
        validate_cache();
#endif
    }

    debug(user_context) << "Exited halide_memoization_cache_release.\n";
}

template<typename KeyType, typename ValueType>
WEAK void HashMap<KeyType, ValueType>::cleanup() {
    debug(NULL) << "halide_memoization_cache_cleanup\n";
    for (size_t i = 0; i < kHashTableSize; i++) {
        CacheEntry<ValueType> *entry = cache_entries[i];
        cache_entries[i] = NULL;
        while (entry != NULL) {
            CacheEntry<ValueType> *next = entry->next;
            entry->destroy();
            halide_free(NULL, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    most_recently_used = NULL;
    least_recently_used = NULL;
    halide_mutex_destroy(&memoization_lock);
}

}}}

#endif//HALIDE_RUNTIME_HASHMAP_H
