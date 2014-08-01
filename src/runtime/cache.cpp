#include "runtime_internal.h"
#include "mini_string.h"
#include "scoped_spin_lock.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#include <stdarg.h>

// This is temporary code. In particular, the hash table is stupid and
// currently thredsafety is accomplished via large granularity spin
// locks. It is mainly intended to prove the programming model and
// runtime interface for memoization. We'll improve the implementation
// later. In the meantime, on some platforms it can be replaced by a
// platform specific LRU cache such as libcache from Apple.

namespace {

#define CACHE_DEBUGGING 0

#if CACHE_DEBUGGING
void debug_print_buffer(void *user_context, const char *buf_name, const buffer_t &buf) {
    halide_printf(user_context, "%s: elem_size %d, (%d, %d, %d) (%d, %d, %d) (%d, %d, %d) (%d, %d, %d)\n",
                  buf_name, buf.elem_size,
                  buf.min[0], buf.extent[0], buf.stride[0],
                  buf.min[1], buf.extent[1], buf.stride[1],
                  buf.min[2], buf.extent[2], buf.stride[2],
                  buf.min[3], buf.extent[3], buf.stride[3]);
}

void debug_print_key(void *user_context, const char *msg, const uint8_t *cache_key, int32_t key_size) {
    halide_printf(user_context, "Key for %s\n", msg);
    for (int i = 0; i < key_size; i++) {
        if (cache_key[i] >= 32 && cache_key[i] <= '~') {
            halide_printf(user_context, "%c", cache_key[i]);
        } else {
            halide_printf(user_context, "%x%x", (cache_key[i] >> 4), cache_key[i] & 0xf);
        }
    }
    halide_printf(user_context, "\n");
}
#endif

size_t full_extent(const buffer_t &buf) {
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

void copy_from_to(void *user_context, const buffer_t &from, buffer_t &to) {
    size_t buffer_size = full_extent(from);;
    halide_assert(user_context, from.elem_size == to.elem_size);
    for (int i = 0; i < 4; i++) {
        halide_assert(user_context, from.extent[i] == to.extent[i]);
        halide_assert(user_context, from.stride[i] == to.stride[i]);
    }
    memcpy(to.host, from.host, buffer_size * from.elem_size);
}

buffer_t copy_of_buffer(void *user_context, const buffer_t &buf) {
    buffer_t result = buf;
    size_t buffer_size = full_extent(result);
    // TODO: ERROR RETURN
    result.host = (uint8_t *)halide_malloc(user_context, buffer_size * result.elem_size);
    copy_from_to(user_context, buf, result);
    return result;
}

bool keys_equal(const uint8_t *key1, const uint8_t *key2, size_t key_size) {
    size_t i = 0;
    return memcmp(key1, key2, key_size) == 0;
}

bool bounds_equal(const buffer_t &buf1, const buffer_t &buf2) {
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
    uint32_t tuple_count;
    buffer_t computed_bounds;
    buffer_t buf;
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
               int32_t tuples, va_list tuple_buffers) :
        user_context(context),
        next(NULL),
        more_recent(NULL),
        less_recent(NULL),
        key_size(cache_key_size),
        hash(key_hash),
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
            buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
            buffer(i) = copy_of_buffer(user_context, *buf);
        }
    }

    ~CacheEntry() {
        halide_free(user_context, key);
        for (int32_t i = 0; i < tuple_count; i++) {
          halide_dev_free(user_context, &buffer(i));
          halide_free(user_context, &buffer(i).host);
        }
    }

    buffer_t &buffer(int32_t i) {
        buffer_t *buf_ptr = &buf;
        return buf_ptr[i];
    }

private:
    CacheEntry(const CacheEntry &) { }
};

uint32_t djb_hash(const uint8_t *key, size_t key_size)  {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
      h = (h << 5) + h + key[i];
    }
    return h;
}

volatile int memoization_lock = 0;

const size_t kHashTableSize = 256;

CacheEntry *cache_entries[kHashTableSize];

CacheEntry *most_recently_used = NULL;
CacheEntry *least_recently_used = NULL;

const uint64_t kDefaultCacheSize = 1 << 20;
int64_t max_cache_size = kDefaultCacheSize;
int64_t current_cache_size = 0;

#if CACHE_DEBUGGING
void validate_cache() {
  halide_printf(NULL, "validating cache\n");
  int entries_in_hash_table = 0;
  for (int i = 0; i < kHashTableSize; i++) {
    CacheEntry *entry = cache_entries[i];
    while (entry != NULL) {
      entries_in_hash_table++;
      if (entry->more_recent == NULL && entry != most_recently_used) {
        halide_printf(NULL, "cache invalid 1\n");
        __builtin_trap();
      }
      if (entry->less_recent == NULL && entry != least_recently_used) {
        halide_printf(NULL, "cache invalid 2\n");
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
  halide_printf(NULL, "hash entries %d, mru entries %d, lru entries %d\n", entries_in_hash_table, entries_from_mru, entries_from_lru);
  if (entries_in_hash_table != entries_from_mru) {
    halide_printf(NULL, "cache invalid 3\n");
    __builtin_trap();
  }
  if (entries_in_hash_table != entries_from_lru) {
    halide_printf(NULL, "cache invalid 4\n");
    __builtin_trap();
  }
}
#endif

void prune_cache() {
#if CACHE_DEBUGGING
    validate_cache();
#endif
    while (current_cache_size > max_cache_size &&
           least_recently_used != NULL) {
        CacheEntry *lru_entry = least_recently_used;
        uint32_t h = lru_entry->hash;
        uint32_t index = h % kHashTableSize;

        CacheEntry *entry = cache_entries[index];
        if (entry == lru_entry) {
            cache_entries[index] = lru_entry->next;
        } else {
            while (entry != NULL && entry->next != lru_entry) {
                entry = entry->next;
            }
            halide_assert(NULL, entry != NULL);
            entry->next = lru_entry->next;
        }
        least_recently_used = lru_entry->more_recent;
        if (least_recently_used != NULL) {
            least_recently_used->less_recent = NULL;
        }
        if (most_recently_used == lru_entry) {
            most_recently_used = NULL;
        }
        for (int32_t i = 0; i < lru_entry->tuple_count; i++) {
            current_cache_size -= full_extent(lru_entry->buffer(i));
        }
        // This code uses placement new, hence placement delete style.
        // (This is because the cache entry has variable size.)
        lru_entry->~CacheEntry();
        halide_free(NULL, lru_entry);
    }
#if CACHE_DEBUGGING
    validate_cache();
#endif
}

} // End anonymous namespace

extern "C" {

WEAK void halide_memoization_cache_set_size(int64_t size) {
    int64_t old_size = max_cache_size;
    if (size == 0) {
        size = kDefaultCacheSize;
    }

    ScopedSpinLock lock(&memoization_lock);

    max_cache_size = size;
    prune_cache();
}

WEAK bool halide_memoization_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size,
                                          buffer_t *computed_bounds, int32_t tuple_count, ...) {
#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_lookup", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        va_list tuple_buffers;
        va_start(tuple_buffers, tuple_count);
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
        va_end(tuple_buffers);
    }
    validate_cache();
#endif

    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedSpinLock lock(&memoization_lock);

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == tuple_count) {

            bool all_bounds_equal = true;

            {
                va_list tuple_buffers;
                va_start(tuple_buffers, tuple_count);
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                    buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
                    all_bounds_equal = bounds_equal(entry->buffer(i), *buf);
                }
                va_end(tuple_buffers);
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

                va_list tuple_buffers;
                va_start(tuple_buffers, tuple_count);
                for (int32_t i = 0; i < tuple_count; i++) {
                    buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
                    copy_from_to(user_context, entry->buffer(i), *buf);
                }
                va_end(tuple_buffers);

                return false;
            }
        }
        entry = entry->next;
    }

#if CACHE_DEBUGGING
    validate_cache();
#endif

    return true;
}

WEAK void halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                         buffer_t *computed_bounds, int32_t tuple_count, ...) {
#if CACHE_DEBUGGING
    debug_print_key(user_context, "halide_memoization_cache_store", cache_key, size);

    debug_print_buffer(user_context, "computed_bounds", *computed_bounds);

    {
        va_list tuple_buffers;
        va_start(tuple_buffers, tuple_count);
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
            debug_print_buffer(user_context, "Allocation bounds", *buf);
        }
        va_end(tuple_buffers);
    }
    validate_cache();
#endif

    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    ScopedSpinLock lock(&memoization_lock);

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size &&
            keys_equal(entry->key, cache_key, size) &&
            bounds_equal(entry->computed_bounds, *computed_bounds) &&
            entry->tuple_count == tuple_count) {

            bool all_bounds_equal = true;

            {
                va_list tuple_buffers;
                va_start(tuple_buffers, tuple_count);
                for (int32_t i = 0; all_bounds_equal && i < tuple_count; i++) {
                    buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
                    all_bounds_equal = bounds_equal(entry->buffer(i), *buf);
                }
                va_end(tuple_buffers);
            }
            if (all_bounds_equal) {
                return;
            }
        }
        entry = entry->next;
    }

    uint64_t added_size = 0;
    {
        va_list tuple_buffers;
        va_start(tuple_buffers, tuple_count);
        for (int32_t i = 0; i < tuple_count; i++) {
            buffer_t *buf = va_arg(tuple_buffers, buffer_t *);
            added_size += full_extent(*buf);
        }
        va_end(tuple_buffers);
    }
    current_cache_size += added_size;
    prune_cache();

    // BUGGY: racey writes?
    void *entry_storage = halide_malloc(user_context, sizeof(CacheEntry) + sizeof(buffer_t) * (tuple_count - 1));
    va_list tuple_buffers;
    va_start(tuple_buffers, tuple_count);
    CacheEntry *new_entry = new (entry_storage) CacheEntry(user_context, cache_key, size, h, *computed_bounds, tuple_count, tuple_buffers);
    va_end(tuple_buffers);

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

#if CACHE_DEBUGGING
    validate_cache();
#endif
}

#if 0
WEAK void halide_memoization_cache_release(void *user_context, const uint8_t *cache_key, int32_t size, buffer_t *computed_bounds, int32_t tuple_count, ...) {
}

}

#endif

}
