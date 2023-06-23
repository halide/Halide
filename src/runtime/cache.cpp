#include "HalideRuntime.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#define CACHE_DEBUGGING 0

#if CACHE_DEBUGGING
WEAK void debug_print_buffer(void *user_context, const char *buf_name, const halide_buffer_t &buf) {
    debug(user_context) << buf_name << ": elem_size " << buf.type.bytes() << " dimensions " << buf.dimensions << ", ";
    for (int i = 0; i < buf.dimensions; i++) {
        debug(user_context) << "(" << buf.dim[i].min
                            << ", " << buf.dim[i].extent
                            << ", " << buf.dim[i].stride << ") ";
    }
    debug(user_context) << "\n";
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
    if ((size_t)key_size > (sizeof(buf) / 2) - 1) {  // Each byte in key can take two bytes in output
        append_ellipses = true;
        key_size = (sizeof(buf) / 2) - 4;  // room for NUL and "..."
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

WEAK bool buffer_has_shape(const halide_buffer_t *buf, const halide_dimension_t *shape) {
    for (int i = 0; i < buf->dimensions; i++) {
        if (buf->dim[i] != shape[i]) {
            return false;
        }
    }
    return true;
}

struct CacheEntry {
    CacheEntry *next;
    CacheEntry *more_recent;
    CacheEntry *less_recent;
    uint8_t *metadata_storage;
    size_t key_size;
    uint8_t *key;
    uint32_t hash;
    uint32_t in_use_count;  // 0 if none returned from halide_cache_lookup
    uint32_t tuple_count;
    // The shape of the computed data. There may be more data allocated than this.
    int32_t dimensions;
    halide_dimension_t *computed_bounds;
    // The actual stored data.
    halide_buffer_t *buf;
    uint64_t eviction_key;
    bool has_eviction_key;

    bool init(const uint8_t *cache_key, size_t cache_key_size,
              uint32_t key_hash,
              const halide_buffer_t *computed_bounds_buf,
              int32_t tuples, halide_buffer_t **tuple_buffers,
              bool has_eviction_key, uint64_t eviction_key);
    void destroy();
    halide_buffer_t &buffer(int32_t i);
};

struct CacheBlockHeader {
    CacheEntry *entry;
    uint32_t hash;
};

// Each host block has extra space to store a header just before the
// contents. This block must respect the same alignment as
// halide_malloc, because it offsets the return value from
// halide_malloc. The header holds the cache key hash and pointer to
// the hash entry.
WEAK __attribute((always_inline)) size_t header_bytes() {
    size_t s = sizeof(CacheBlockHeader);
    size_t mask = ::halide_internal_malloc_alignment() - 1;
    return (s + mask) & ~mask;
}

WEAK CacheBlockHeader *get_pointer_to_header(uint8_t *host) {
    return (CacheBlockHeader *)(host - header_bytes());
}

WEAK bool CacheEntry::init(const uint8_t *cache_key, size_t cache_key_size,
                           uint32_t key_hash, const halide_buffer_t *computed_bounds_buf,
                           int32_t tuples, halide_buffer_t **tuple_buffers,
                           bool has_eviction_key_arg, uint64_t eviction_key_arg) {
    next = nullptr;
    more_recent = nullptr;
    less_recent = nullptr;
    key_size = cache_key_size;
    hash = key_hash;
    in_use_count = 0;
    tuple_count = tuples;
    dimensions = computed_bounds_buf->dimensions;

    // Allocate all the necessary space (or die)
    size_t storage_bytes = 0;

    // First storage for the tuple halide_buffer_t's
    storage_bytes += sizeof(halide_buffer_t) * tuple_count;

    // Then storage for the computed shape, and the allocated shape for
    // each tuple buffer. These may all be distinct.
    size_t shape_offset = storage_bytes;
    storage_bytes += sizeof(halide_dimension_t) * dimensions * (tuple_count + 1);

    // Then storage for the key
    size_t key_offset = storage_bytes;
    storage_bytes += key_size;

    // Do the single malloc call
    metadata_storage = (uint8_t *)halide_malloc(nullptr, storage_bytes);
    if (!metadata_storage) {
        return false;
    }

    // Set up the pointers into the allocated metadata space
    buf = (halide_buffer_t *)metadata_storage;
    computed_bounds = (halide_dimension_t *)(metadata_storage + shape_offset);
    key = metadata_storage + key_offset;

    // Copy over the key
    for (size_t i = 0; i < key_size; i++) {
        key[i] = cache_key[i];
    }

    // Copy over the shape of the computed region
    for (int i = 0; i < dimensions; i++) {
        computed_bounds[i] = computed_bounds_buf->dim[i];
    }

    // Copy over the tuple buffers and the shapes of the allocated regions
    for (uint32_t i = 0; i < tuple_count; i++) {
        buf[i] = *tuple_buffers[i];
        buf[i].dim = computed_bounds + (i + 1) * dimensions;
        for (int j = 0; j < dimensions; j++) {
            buf[i].dim[j] = tuple_buffers[i]->dim[j];
        }
    }

    has_eviction_key = has_eviction_key_arg;
    eviction_key = eviction_key_arg;
    return true;
}

WEAK void CacheEntry::destroy() {
    for (uint32_t i = 0; i < tuple_count; i++) {
        if (halide_device_free(nullptr, &buf[i]) != 0) {
            // Just log a debug message, there's not much we can do in response here
            debug(nullptr) << "CacheEntry::destroy: halide_device_free failed\n";
        }
        halide_free(nullptr, get_pointer_to_header(buf[i].host));
    }
    halide_free(nullptr, metadata_storage);
}

WEAK uint32_t djb_hash(const uint8_t *key, size_t key_size) {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
        h = (h << 5) + h + key[i];
    }
    return h;
}

WEAK halide_mutex memoization_lock = {{0}};

const size_t kHashTableSize = 256;

WEAK CacheEntry *cache_entries[kHashTableSize];

WEAK CacheEntry *most_recently_used = nullptr;
WEAK CacheEntry *least_recently_used = nullptr;

const uint64_t kDefaultCacheSize = 1 << 20;
WEAK int64_t max_cache_size = kDefaultCacheSize;
WEAK int64_t current_cache_size = 0;

#if CACHE_DEBUGGING
WEAK void validate_cache() {
    print(nullptr) << "validating cache, "
                   << "current size " << current_cache_size
                   << " of maximum " << max_cache_size << "\n";
    int entries_in_hash_table = 0;
    for (size_t i = 0; i < kHashTableSize; i++) {
        CacheEntry *entry = cache_entries[i];
        while (entry != nullptr) {
            entries_in_hash_table++;
            if (entry->more_recent == nullptr && entry != most_recently_used) {
                halide_print(nullptr, "cache invalid case 1\n");
                __builtin_trap();
            }
            if (entry->less_recent == nullptr && entry != least_recently_used) {
                halide_print(nullptr, "cache invalid case 2\n");
                __builtin_trap();
            }
            entry = entry->next;
        }
    }
    int entries_from_mru = 0;
    CacheEntry *mru_chain = most_recently_used;
    while (mru_chain != nullptr) {
        entries_from_mru++;
        mru_chain = mru_chain->less_recent;
    }
    int entries_from_lru = 0;
    CacheEntry *lru_chain = least_recently_used;
    while (lru_chain != nullptr) {
        entries_from_lru++;
        lru_chain = lru_chain->more_recent;
    }
    print(nullptr) << "hash entries " << entries_in_hash_table
                   << ", mru entries " << entries_from_mru
                   << ", lru entries " << entries_from_lru << "\n";
    if (entries_in_hash_table != entries_from_mru) {
        halide_print(nullptr, "cache invalid case 3\n");
        __builtin_trap();
    }
    if (entries_in_hash_table != entries_from_lru) {
        halide_print(nullptr, "cache invalid case 4\n");
        __builtin_trap();
    }
    if (current_cache_size < 0) {
        halide_print(nullptr, "cache size is negative\n");
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
                halide_abort_if_false(nullptr, prev_hash_entry != nullptr);
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
            for (uint32_t i = 0; i < prune_candidate->tuple_count; i++) {
                current_cache_size -= prune_candidate->buf[i].size_in_bytes();
            }

            // Deallocate the entry.
            prune_candidate->destroy();
            halide_free(nullptr, prune_candidate);
        }

        prune_candidate = more_recent;
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
                                         halide_buffer_t *computed_bounds, int32_t tuple_count, halide_buffer_t **tuple_buffers) {
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
            keys_equal(entry->key, cache_key, size) &&
            buffer_has_shape(computed_bounds, entry->computed_bounds) &&
            entry->tuple_count == (uint32_t)tuple_count) {

            // Check all the tuple buffers have the same bounds (they should).
            bool all_bounds_equal = true;
            for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                all_bounds_equal = buffer_has_shape(tuple_buffers[i], entry->buf[i].dim);
            }

            if (all_bounds_equal) {
                if (entry != most_recently_used) {
                    halide_abort_if_false(user_context, entry->more_recent != nullptr);
                    if (entry->less_recent != nullptr) {
                        entry->less_recent->more_recent = entry->more_recent;
                    } else {
                        halide_abort_if_false(user_context, least_recently_used == entry);
                        least_recently_used = entry->more_recent;
                    }
                    halide_abort_if_false(user_context, entry->more_recent != nullptr);
                    entry->more_recent->less_recent = entry->less_recent;

                    entry->more_recent = nullptr;
                    entry->less_recent = most_recently_used;
                    if (most_recently_used != nullptr) {
                        most_recently_used->more_recent = entry;
                    }
                    most_recently_used = entry;
                }

                for (int32_t i = 0; i < tuple_count; i++) {
                    halide_buffer_t *buf = tuple_buffers[i];
                    *buf = entry->buf[i];
                }

                entry->in_use_count += tuple_count;

                return 0;
            }
        }
        entry = entry->next;
    }

    for (int32_t i = 0; i < tuple_count; i++) {
        halide_buffer_t *buf = tuple_buffers[i];

        buf->host = ((uint8_t *)halide_malloc(user_context, buf->size_in_bytes() + header_bytes()));
        if (buf->host == nullptr) {
            for (int32_t j = i; j > 0; j--) {
                halide_free(user_context, get_pointer_to_header(tuple_buffers[j - 1]->host));
                tuple_buffers[j - 1]->host = nullptr;
            }
            return -1;
        }
        buf->host += header_bytes();
        CacheBlockHeader *header = get_pointer_to_header(buf->host);
        header->hash = h;
        header->entry = nullptr;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif

    return 1;
}

WEAK int halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                        halide_buffer_t *computed_bounds,
                                        int32_t tuple_count, halide_buffer_t **tuple_buffers,
                                        bool has_eviction_key, uint64_t eviction_key) {
    debug(user_context) << "halide_memoization_cache_store has_eviction_key: " << has_eviction_key << " eviction_key " << eviction_key << " .\n";

    uint32_t h = get_pointer_to_header(tuple_buffers[0]->host)->hash;

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

    CacheEntry *entry = cache_entries[index];
    while (entry != nullptr) {
        if (entry->hash == h && entry->key_size == (size_t)size &&
            keys_equal(entry->key, cache_key, size) &&
            buffer_has_shape(computed_bounds, entry->computed_bounds) &&
            entry->tuple_count == (uint32_t)tuple_count) {

            bool all_bounds_equal = true;
            bool no_host_pointers_equal = true;
            {
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                    halide_buffer_t *buf = tuple_buffers[i];
                    all_bounds_equal = buffer_has_shape(tuple_buffers[i], entry->buf[i].dim);
                    if (entry->buf[i].host == buf->host) {
                        no_host_pointers_equal = false;
                    }
                }
            }
            if (all_bounds_equal) {
                halide_abort_if_false(user_context, no_host_pointers_equal);
                // This entry is still in use by the caller. Mark it as having no cache entry
                // so halide_memoization_cache_release can free the buffer.
                for (int32_t i = 0; i < tuple_count; i++) {
                    get_pointer_to_header(tuple_buffers[i]->host)->entry = nullptr;
                }
                return halide_error_code_success;
            }
        }
        entry = entry->next;
    }

    uint64_t added_size = 0;
    {
        for (int32_t i = 0; i < tuple_count; i++) {
            halide_buffer_t *buf = tuple_buffers[i];
            added_size += buf->size_in_bytes();
        }
    }
    current_cache_size += added_size;
    prune_cache();

    CacheEntry *new_entry = (CacheEntry *)halide_malloc(nullptr, sizeof(CacheEntry));
    bool inited = false;
    if (new_entry) {
        inited = new_entry->init(cache_key, size, h, computed_bounds, tuple_count, tuple_buffers,
                                 has_eviction_key, eviction_key);
    }
    if (!inited) {
        current_cache_size -= added_size;

        // This entry is still in use by the caller. Mark it as having no cache entry
        // so halide_memoization_cache_release can free the buffer.
        for (int32_t i = 0; i < tuple_count; i++) {
            get_pointer_to_header(tuple_buffers[i]->host)->entry = nullptr;
        }

        if (new_entry) {
            halide_free(user_context, new_entry);
        }
        return halide_error_code_success;
    }

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

    new_entry->in_use_count = tuple_count;

    for (int32_t i = 0; i < tuple_count; i++) {
        get_pointer_to_header(tuple_buffers[i]->host)->entry = new_entry;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif
    debug(user_context) << "Exiting halide_memoization_cache_store\n";

    return halide_error_code_success;
}

WEAK void halide_memoization_cache_release(void *user_context, void *host) {
    CacheBlockHeader *header = get_pointer_to_header((uint8_t *)host);
    debug(user_context) << "halide_memoization_cache_release\n";
    CacheEntry *entry = header->entry;

    if (entry == nullptr) {
        halide_free(user_context, header);
    } else {
        ScopedMutexLock lock(&memoization_lock);

        halide_abort_if_false(user_context, entry->in_use_count > 0);
        entry->in_use_count--;
#if CACHE_DEBUGGING
        validate_cache();
#endif
    }

    debug(user_context) << "Exited halide_memoization_cache_release.\n";
}

WEAK void halide_memoization_cache_cleanup() {
    debug(nullptr) << "halide_memoization_cache_cleanup\n";
    for (auto &entry_ref : cache_entries) {
        CacheEntry *entry = entry_ref;
        entry_ref = nullptr;
        while (entry != nullptr) {
            CacheEntry *next = entry->next;
            entry->destroy();
            halide_free(nullptr, entry);
            entry = next;
        }
    }
    current_cache_size = 0;
    most_recently_used = nullptr;
    least_recently_used = nullptr;
}

WEAK void halide_memoization_cache_evict(void *user_context, uint64_t eviction_key) {
    ScopedMutexLock lock(&memoization_lock);

    for (auto &entry_ref : cache_entries) {
        CacheEntry *entry = entry_ref;
        if (entry != nullptr) {
            CacheEntry **prev = &entry_ref;
            while (entry != nullptr) {
                CacheEntry *next = entry->next;
                if (entry->has_eviction_key && entry->eviction_key == eviction_key) {
                    *prev = next;
                    if (entry->more_recent != nullptr) {
                        entry->more_recent->less_recent = entry->less_recent;
                    } else {
                        most_recently_used = entry->less_recent;
                    }
                    if (entry->less_recent != nullptr) {
                        entry->less_recent->more_recent = entry->more_recent;
                    } else {
                        least_recently_used = entry->more_recent;
                    }
                    entry->destroy();
                    halide_free(user_context, entry);
                } else {
                    prev = &entry->next;
                }
                entry = next;
            }
        }
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

namespace {

WEAK __attribute__((destructor)) void halide_cache_cleanup() {
    halide_memoization_cache_cleanup();
}

}  // namespace
}
