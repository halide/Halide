#include "HalideRuntime.h"
#include "device_buffer_utils.h"
#include "printer.h"
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
    if ((size_t)key_size > (sizeof(buf) / 2) - 1) { // Each byte in key can take two bytes in output
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

// Each host block has extra space to store a header just before the contents.
// 16 is chosen to keep that alignment.
// The header holds the cache key hash and pointer to the hash entry.
//
// This is an optimization the number of cycles it takes for the cache
// to operate.
const size_t extra_bytes_host_bytes = 16;

struct CacheEntry {
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

    bool init(const uint8_t *cache_key, size_t cache_key_size,
              uint32_t key_hash, const buffer_t &computed_buf,
              int32_t tuples, buffer_t **tuple_buffers);
    void destroy();
    buffer_t &buffer(int32_t i);

};

struct CacheBlockHeader {
    CacheEntry *entry;
    uint32_t hash;
};

WEAK CacheBlockHeader *get_pointer_to_header(uint8_t * host) {
    return (CacheBlockHeader *)(host - extra_bytes_host_bytes);
}

WEAK bool CacheEntry::init(const uint8_t *cache_key, size_t cache_key_size,
                           uint32_t key_hash, const buffer_t &computed_buf,
                           int32_t tuples, buffer_t **tuple_buffers) {
    next = NULL;
    more_recent = NULL;
    less_recent = NULL;
    key_size = cache_key_size;
    hash = key_hash;
    in_use_count = 0;
    tuple_count = tuples;

    key = (uint8_t *)halide_malloc(NULL, key_size);
    if (key == NULL) {
        return false;
    }
    computed_bounds = computed_buf;
    computed_bounds.host = NULL;
    computed_bounds.dev = 0;
    for (size_t i = 0; i < key_size; i++) {
        key[i] = cache_key[i];
    }
    for (uint32_t i = 0; i < tuple_count; i++) {
        buffer(i) = *tuple_buffers[i];
    }
    return true;
}

WEAK void CacheEntry::destroy() {
    halide_free(NULL, key);
    for (uint32_t i = 0; i < tuple_count; i++) {
        halide_device_free(NULL, &buffer(i));
        halide_free(NULL, get_pointer_to_header(buffer(i).host));
    }
}

WEAK buffer_t &CacheEntry::buffer(int32_t i) {
    buffer_t *buf_ptr = &buf[0];
    return buf_ptr[i];
}

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
    for (size_t i = 0; i < kHashTableSize; i++) {
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

            // Decrease cache used amount.
            for (uint32_t i = 0; i < prune_candidate->tuple_count; i++) {
                current_cache_size -= buf_size(&prune_candidate->buffer(i));
            }

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

WEAK int halide_memoization_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size,
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
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == (uint32_t)tuple_count) {

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
                    *buf = entry->buffer(i);
                }

                entry->in_use_count += tuple_count;

                return 0;
            }
        }
        entry = entry->next;
    }

    for (int32_t i = 0; i < tuple_count; i++) {
        buffer_t *buf = tuple_buffers[i];

        // See documentation on extra_bytes_host_bytes
        buf->host = ((uint8_t *)halide_malloc(user_context, buf_size(buf) + extra_bytes_host_bytes));
        if (buf->host == NULL) {
            for (int32_t j = i; j > 0; j--) {
                halide_free(user_context, get_pointer_to_header(tuple_buffers[j - 1]->host));
                tuple_buffers[j - 1]->host = NULL;
            }
            return -1;
        }
        buf->host += extra_bytes_host_bytes;
        CacheBlockHeader *header = get_pointer_to_header(buf->host);
        header->hash = h;
        header->entry = NULL;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif

    return 1;
}

WEAK int halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                        buffer_t *computed_bounds, int32_t tuple_count, buffer_t **tuple_buffers) {
    debug(user_context) << "halide_memoization_cache_store\n";

    uint32_t h = get_pointer_to_header(tuple_buffers[0]->host)->hash;

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
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == (uint32_t)tuple_count) {

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
                // This entry is still in use by the caller. Mark it as having no cache entry
                // so halide_memoization_cache_release can free the buffer.
                for (int32_t i = 0; i < tuple_count; i++) {
                    get_pointer_to_header(tuple_buffers[i]->host)->entry = NULL;

                }
                return 0;
            }
        }
        entry = entry->next;
    }

    uint64_t added_size = 0;
    {
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = tuple_buffers[i];
            added_size += buf_size(buf);
        }
    }
    current_cache_size += added_size;
    prune_cache();

    void *entry_storage = halide_malloc(NULL, sizeof(CacheEntry) + sizeof(buffer_t) * (tuple_count - 1));
    if (entry_storage == NULL) {
        current_cache_size -= added_size;

        // This entry is still in use by the caller. Mark it as having no cache entry
        // so halide_memoization_cache_release can free the buffer.
        for (int32_t i = 0; i < tuple_count; i++) {
            get_pointer_to_header(tuple_buffers[i]->host)->entry = NULL;
        }
        return 0;
    }

    CacheEntry *new_entry = (CacheEntry *)entry_storage;
    bool inited = new_entry->init(cache_key, size, h, *computed_bounds, tuple_count, tuple_buffers);
    if (!inited) {
        current_cache_size -= added_size;

        // This entry is still in use by the caller. Mark it as having no cache entry
        // so halide_memoization_cache_release can free the buffer.
        for (int32_t i = 0; i < tuple_count; i++) {
            get_pointer_to_header(tuple_buffers[i]->host)->entry = NULL;
        }

        halide_free(user_context, new_entry);
        return 0;
    }

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

    new_entry->in_use_count = tuple_count;

    for (int32_t i = 0; i < tuple_count; i++) {
        get_pointer_to_header(tuple_buffers[i]->host)->entry = new_entry;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif
    debug(user_context) << "Exiting halide_memoization_cache_store\n";

    return 0;
}

WEAK void halide_memoization_cache_release(void *user_context, void *host) {
    CacheBlockHeader *header = get_pointer_to_header((uint8_t *)host);
    debug(user_context) << "halide_memoization_cache_release\n";
    CacheEntry *entry = header->entry;

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

WEAK void halide_memoization_cache_cleanup() {
    debug(NULL) << "halide_memoization_cache_cleanup\n";
    for (size_t i = 0; i < kHashTableSize; i++) {
        CacheEntry *entry = cache_entries[i];
        cache_entries[i] = NULL;
        while (entry != NULL) {
            CacheEntry *next = entry->next;
            entry->destroy();
            halide_free(NULL, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    halide_mutex_destroy(&memoization_lock);
}

namespace {

__attribute__((destructor))
WEAK void halide_cache_cleanup() {
    halide_memoization_cache_cleanup();
}

}

}
