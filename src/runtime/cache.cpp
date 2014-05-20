// This is temporary code. In particular, the hash table is stupid and currently not thredsafe.
// It is mainly intended to get the interface right, then we'll figure out the right way to
// implement something better. (Ultimately, it is not clear how many use cases this can really
// cover and thus it is easily replaceable by the application.)

#include "mini_stdint.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

namespace {

struct CacheEntry {
    CacheEntry *next;
    size_t key_size;
    uint8_t *key;
    uint32_t hash;
    buffer_t buf;
};

uint32_t djb_hash(const uint8_t *key, size_t key_size)  {
    uint32_t h = 5381;
    for (size_t i = 0; i < key_size; i++) {
      h = (h << 5) + h + key[i];
    }
    return h;
}

const size_t kHashTableSize = 256;

CacheEntry *cache_entries[kHashTableSize];

void copy_from_to(void *user_context, const buffer_t &from, buffer_t &to) {
    size_t full_extent = 0;
    halide_assert(user_context, from.elem_size == to.elem_size);
    for (int i = 0; i < 4; i++) {
        halide_assert(user_context, from.extent[i] == to.extent[i]);
        halide_assert(user_context, from.stride[i] == to.stride[i]);
	if ((from.extent[i] * from.stride[i]) > full_extent) {
	    full_extent = from.extent[i] * from.stride[i];
	}
    }
    // TODO: fix cheesy memcpy replacement.
    for (size_t i = 0; i < full_extent * from.elem_size; i++) {
      to.host[i] = from.host[i];
    }
}

buffer_t copy_of_buffer(void *user_context, const buffer_t &buf) {
    buffer_t result = buf;
    size_t full_extent = 0;
    for (int i = 0; i < 4; i++) {
        if ((result.extent[i] * result.stride[i]) > full_extent) {
            full_extent = result.extent[i] * result.stride[i];
	}
    }
    result.host = (uint8_t *)halide_malloc(user_context, full_extent * result.elem_size);
    copy_from_to(user_context, buf, result);
    return result;
}

bool keys_equal(const uint8_t *key1, const uint8_t *key2, size_t key_size) {
    size_t i = 0;
    while (i < key_size &&
	   key1[i] == key2[i]) {
        i++;
    }
    return i == key_size;
}

extern "C" {

WEAK bool halide_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size, buffer_t *buf) {
#if 0
  halide_printf(user_context, "halide_cache_lookup called key size is %d.\n", size);
  for (int i = 0; i < size; i++) {
    halide_printf(user_context, "%x%x", (cache_key[i] >> 4), cache_key[i] & 0xf);
  }
  halide_printf(user_context, "\n");
#endif
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size && keys_equal(entry->key, cache_key, size)) {
	    // Deal with bounds?
	    copy_from_to(user_context, entry->buf, *buf);
	    //	    halide_printf(user_context, "halide_cache_lookup returning true.\n");
	    return false;
        }
    }

    //    halide_printf(user_context, "halide_cache_lookup returning false.\n");
    return true;
}

WEAK void halide_cache_store(void *user_context, const uint8_t *cache_key, int32_t size, buffer_t *buf) {
#if 0
  halide_printf(user_context, "halide_cache_store called key size is %d.\n", size);
  for (int i = 0; i < size; i++) {
    halide_printf(user_context, "%x%x", (cache_key[i] >> 4), cache_key[i] & 0xf);
  }
  halide_printf(user_context, "\n");
#endif
    uint32_t h = djb_hash(cache_key, size);
    uint32_t index = h % kHashTableSize;

    CacheEntry *entry = cache_entries[index];
    while (entry != NULL) {
        if (entry->hash == h && entry->key_size == size && keys_equal(entry->key, cache_key, size)) {
	  halide_assert(user_context, false);
        }
    }

    // BUGGY: racey writes?
    CacheEntry *new_entry = (CacheEntry *)halide_malloc(user_context, sizeof(CacheEntry));
    new_entry->key_size = size;
    // TODO: ERROR RETURN
    new_entry->key = (uint8_t *)halide_malloc(user_context, size);
    for (size_t i = 0; i < size; i++) {
      new_entry->key[i] = cache_key[i];
    }
    new_entry->hash = h;
    new_entry->buf = copy_of_buffer(user_context,*buf);

    new_entry->next = cache_entries[index];
    cache_entries[index] = new_entry;
}

#if 0
WEAK void halide_cache_release(void *user_context, const uint8_t *cache_key, int32_t size, buffer_t *buf) {
    std::string key(cache_key, size);
}
#endif

}

}
