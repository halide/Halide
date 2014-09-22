#include "runtime_internal.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"
#include "scoped_mutex_lock.h"

// This is temporary code. In particular, the hash table is stupid and
// currently thread safety is accomplished via large granularity spin
// locks. It is mainly intended to prove the programming model and
// runtime interface for memoization. We'll improve the implementation
// later. In the meantime, on some platforms it can be replaced by a
// platform specific LRU cache such as libcache from Apple.

namespace Halide { namespace Runtime { namespace Internal {

#define CACHE_DEBUGGING 0

#if CACHE_DEBUGGING
WEAK void debug_print_buffer(void *user_context, const char *buf_name, const buffer_t &buf) {
    debug(user_context) << buf_name
                        << ": elem_size " << buf.elem_size << ", "
                        << "(" << buf.min[0] << ", " << buf.extent[0] << ", " << buf.stride[0] << ") "
                        << "(" << buf.min[1] << ", " << buf.extent[1] << ", " << buf.stride[1] << ") "
                        << "(" << buf.min[2] << ", " << buf.extent[2] << ", " << buf.stride[2] << ") "
                        << "(" << buf.min[3] << ", " << buf.extent[3] << ", " << buf.stride[3] << ")\n";
}

WEAK char to_hex_char(int val) {
    if (val < 10) {
        return '0' + val;
    }
    return 'A' + (val - 10);
}

WEAK void debug_print_key(void *user_context, const char *msg, const uint8_t *cache_key, int32_t key_size) {
    debug(user_context) << "Key for " << msg << "\n";
    char buf[1024];
    bool append_ellipses = false;
    if (key_size > (sizeof(buf) / 2) - 1) { // Each byte in key can take two bytes in output
        append_ellipses = true;
        key_size = (sizeof(buf) / 2) - 4; // room for NUL and "..."
    }
    char *buf_ptr = buf;
    for (int i = 0; i < key_size; i++) {
        if (cache_key[i] >= 32 && cache_key[i] <= '~') {
            *buf_ptr++ = cache_key[i];
        } else {
            *buf_ptr++ = to_hex_char((cache_key[i] >> 4));
            *buf_ptr++ = to_hex_char((cache_key[i] & 0xf));
        }
    }
    if (append_ellipses) {
        *buf_ptr++ = '.';
        *buf_ptr++ = '.';
        *buf_ptr++ = '.';
    }
    *buf_ptr++ = '\0';
    debug(user_context) << buf << "\n";
}
#endif

WEAK size_t full_extent(const buffer_t &buf) {
    size_t result = 1;
    for (int i = 0; i < 4; i++) {
        int32_t stride = buf.stride[i];
        if (stride < 0) stride = -stride;
        if ((buf.extent[i] * stride) > result) {
            result = buf.extent[i] * stride;
        }
    }
    return result;
}

WEAK void copy_from_to(void *user_context, const buffer_t &from, buffer_t &to) {
    halide_assert(user_context, from.elem_size == to.elem_size);
    for (int i = 0; i < 4; i++) {
        halide_assert(user_context, from.extent[i] == to.extent[i]);
        halide_assert(user_context, from.stride[i] == to.stride[i]);
    }
#if 0
    size_t buffer_size = full_extent(from);;
    memcpy(to.host, from.host, buffer_size * from.elem_size);
#else
    to.host = from.host;
#endif
}

WEAK buffer_t copy_of_buffer(void *user_context, const buffer_t &buf) {
    buffer_t result = buf;
#if 0
    size_t buffer_size = full_extent(result);
    // TODO: ERROR RETURN
    result.host = (uint8_t *)halide_malloc(user_context, buffer_size * result.elem_size);
#else   
    copy_from_to(user_context, buf, result);
#endif
    return result;
}

WEAK bool keys_equal(const uint8_t *key1, const uint8_t *key2, size_t key_size) {
    return memcmp(key1, key2, key_size) == 0;
}

WEAK bool bounds_equal(const buffer_t &buf1, const buffer_t &buf2) {
    if (buf1.elem_size != buf2.elem_size)
        return false;
    for (size_t i = 0; i < 4; i++) {
        if (buf1.min[i] != buf2.min[i] ||
            buf1.extent[i] != buf2.extent[i] ||
            buf1.stride[i] != buf2.stride[i]) {
            return false;
        }
    }
    return true;
}

struct CacheEntry {
    void *user_context; // Is this a good idea at all? Perhaps a call to clear the cache off all entries for a given user context?
    CacheEntry *next;
    CacheEntry *more_recent;
    CacheEntry *less_recent;
    size_t key_size;
    uint8_t *key;
    uint32_t hash;
    uint32_t in_use_count; // 0 if none returned from halide_cache_lookup
    uint32_t tuple_count;
    buffer_t computed_bounds;
    buffer_t buf[1];
    // ADDITIONAL buffer_t STRUCTS HERE

    // Allow placement new with constructor
    void *operator new(size_t size, void *storage) {
        return storage;
    }

#if 0
    void operator delete(void *ptr) {
        halide_free(NULL, ptr);
    }
#endif

    CacheEntry(void *context, const uint8_t *cache_key, size_t cache_key_size,
               uint32_t key_hash, const buffer_t &computed_buf,
               int32_t tuples, buffer_t **tuple_buffers) :
        user_context(context),
        next(NULL),
        more_recent(NULL),
        less_recent(NULL),
        key_size(cache_key_size),
        hash(key_hash),
        in_use_count(0),
        tuple_count(tuples) {
        // TODO: ERROR RETURN
        key = (uint8_t *)halide_malloc(user_context, key_size);
        computed_bounds = computed_buf;
        computed_bounds.host = NULL;
        computed_bounds.dev = 0;
        for (size_t i = 0; i < key_size; i++) {
            key[i] = cache_key[i];
        }
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            buffer(i) = copy_of_buffer(user_context, *buf);
        }
    }

    ~CacheEntry() {
        halide_free(user_context, key);
        for (int32_t i = 0; i < tuple_count; i++) {
          halide_dev_free(user_context, &buffer(i));
          halide_free(user_context, buffer(i).host);
        }
    }

    buffer_t &buffer(int32_t i) {
        buffer_t *buf_ptr = &buf[0];
        return buf_ptr[i];
    }

private:
    CacheEntry(const CacheEntry &) { }
};

WEAK uint32_t djb_hash(const uint8_t *key, size_t key_size)  {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
      h = (h << 5) + h + key[i];
    }
    return h;
}

WEAK halide_mutex memoization_lock;

const size_t kHashTableSize = 256;

WEAK CacheEntry *cache_entries[kHashTableSize];

WEAK CacheEntry *most_recently_used = NULL;
WEAK CacheEntry *least_recently_used = NULL;

const uint64_t kDefaultCacheSize = 1 << 20;
WEAK int64_t max_cache_size = kDefaultCacheSize;
WEAK int64_t current_cache_size = 0;

#if CACHE_DEBUGGING
WEAK void validate_cache() {
    print(NULL) << "validating cache, "
                << "current size " << current_cache_size
                << " of maximum " << max_cache_size << "\n";
    int entries_in_hash_table = 0;
    for (int i = 0; i < kHashTableSize; i++) {
        CacheEntry *entry = cache_entries[i];
        while (entry != NULL) {
            entries_in_hash_table++;
            if (entry->more_recent == NULL && entry != most_recently_used) {
                halide_print(NULL, "cache invalid case 1\n");
                __builtin_trap();
            }
            if (entry->less_recent == NULL && entry != least_recently_used) {
                halide_print(NULL, "cache invalid case 2\n");
                __builtin_trap();
            }
            entry = entry->next;
        }
    }
    int entries_from_mru = 0;
    CacheEntry *mru_chain = most_recently_used;
    while (mru_chain != NULL) {
        entries_from_mru++;
        mru_chain = mru_chain->less_recent;
    }
    int entries_from_lru = 0;
    CacheEntry *lru_chain = least_recently_used;
    while (lru_chain != NULL) {
        entries_from_lru++;
        lru_chain = lru_chain->more_recent;
    }
    print(NULL) << "hash entries " << entries_in_hash_table
                << ", mru entries " << entries_from_mru
                << ", lru entries " << entries_from_lru << "\n";
    if (entries_in_hash_table != entries_from_mru) {
        halide_print(NULL, "cache invalid case 3\n");
        __builtin_trap();
    }
    if (entries_in_hash_table != entries_from_lru) {
        halide_print(NULL, "cache invalid case 4\n");
        __builtin_trap();
    }
}
#endif

WEAK void prune_cache() {
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

            // Decrease cache used amount
            for (int32_t i = 0; i < prune_candidate->tuple_count; i++) {
                current_cache_size -= full_extent(prune_candidate->buffer(i));
            }

            // This code uses placement new, hence placement delete style.
            // (This is because the cache entry has variable size.)
            void *entry_user_context = prune_candidate->user_context;
            prune_candidate->~CacheEntry();
            halide_free(entry_user_context, prune_candidate);
        }

        prune_candidate = more_recent;
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_memoization_cache_set_size(int64_t size) {
    if (size == 0) {
        size = kDefaultCacheSize;
    }

    ScopedMutexLock lock(&memoization_lock);

    max_cache_size = size;
    prune_cache();
}

WEAK bool halide_memoization_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size,
                                          buffer_t *computed_bounds, int32_t tuple_count, buffer_t **tuple_buffers) {
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedMutexLock lock(&memoization_lock);

#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_lookup", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
    }
#endif

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == tuple_count) {

            bool all_bounds_equal = true;

            {
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                    buffer_t *buf = tuple_buffers[i];
                    all_bounds_equal = bounds_equal(entry->buffer(i), *buf);
                }
            }

            if (all_bounds_equal) {
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

                for (int32_t i = 0; i < tuple_count; i++) {
                    buffer_t *buf = tuple_buffers[i];
                    //copy_from_to(user_context, entry->buffer(i), *buf);
                    *buf = entry->buffer(i);
                }

                entry->in_use_count++;

                return false;
            }
        }
        entry = entry->next;
    }

    for (int32_t i = 0; i < tuple_count; i++) {
        buffer_t *buf = tuple_buffers[i];
        size_t buffer_size = full_extent(*buf);
        // TODO: ERROR RETURN
        buf->host = (uint8_t *)halide_malloc(user_context, buffer_size * buf->elem_size);
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif

    return true;
}

WEAK void halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                         buffer_t *computed_bounds, int32_t tuple_count, buffer_t **tuple_buffers) {
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedMutexLock lock(&memoization_lock);

#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_store", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
    }
#endif

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == tuple_count) {

            bool all_bounds_equal = true;
            bool no_host_pointers_equal = true;
            {
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                    buffer_t *buf = tuple_buffers[i];
                    all_bounds_equal = bounds_equal(entry->buffer(i), *buf);
                    if (entry->buffer(i).host == buf->host) {
                        no_host_pointers_equal = false;
                    }
                }
            }
            if (all_bounds_equal) {
                halide_assert(user_context, no_host_pointers_equal);
                for (int32_t i = 0; i < tuple_count; i++) {
		    halide_dev_free(user_context, tuple_buffers[i]);
                    halide_free(user_context, tuple_buffers[i]->host);
                }               
                return;
            }
        }
        entry = entry->next;
    }

    uint64_t added_size = 0;
    {
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            added_size += full_extent(*buf);
        }
    }
    current_cache_size += added_size;
    prune_cache();

    void *entry_storage = halide_malloc(user_context, sizeof(CacheEntry) + sizeof(buffer_t) * (tuple_count - 1));
    CacheEntry *new_entry =
        new (entry_storage) CacheEntry(user_context, cache_key, size, h, *computed_bounds, tuple_count, tuple_buffers);

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
}

WEAK void halide_memoization_cache_release(void *user_context, const uint8_t *cache_key, int32_t size, buffer_t *computed_bounds, int32_t tuple_count, buffer_t **tuple_buffers) {
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedMutexLock lock(&memoization_lock);

#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_release", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
    }
#endif

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size &&
            keys_equal(entry->key, cache_key, size) && 
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == tuple_count) {
            
            bool all_bounds_equal = true;

            {
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
		    buffer_t *buf = tuple_buffers[i];
                    all_bounds_equal = bounds_equal(entry->buffer(i), *buf);
                }
            }
            if (all_bounds_equal) {
                halide_assert(user_context, entry->in_use_count > 0);
                entry->in_use_count--;
                return;
            }
        }
        entry = entry->next;
    }
    halide_assert(user_context, false);
}

WEAK void halide_memoization_cache_cleanup() {
    for (int i = 0; i < kHashTableSize; i++) {
        CacheEntry *entry = cache_entries[i];
        cache_entries[i] = NULL;
        while (entry != NULL) {
            CacheEntry *next = entry->next;
            void *user_context = entry->user_context;
            entry->~CacheEntry();
            halide_free(user_context, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    halide_mutex_cleanup(&memoization_lock);
}

}
