#include "HalideRuntime.h"

#include "common.h"

#include "alignment_128.cpp"
#include "cache.cpp"

using namespace Halide::Runtime::Internal;

extern "C" {

void *halide_malloc(void *user_context, size_t x) {
    return allocate_system(user_context, x);
}

void halide_free(void *user_context, void *ptr) {
    deallocate_system(user_context, ptr);
}

int halide_device_free(void *user_context, struct halide_buffer_t *buf) {
    return 0;
}

void halide_mutex_lock(struct halide_mutex *mutex) {
}

void halide_mutex_unlock(struct halide_mutex *mutex) {
}

}  // extern "C"

namespace {

constexpr int entry_size = 1024;

// A one-tuple cache entry, stored under a one-byte key.
struct Entry {
    uint8_t key;
    halide_dimension_t computed_dim{0, entry_size, 1};
    halide_dimension_t dim{0, entry_size, 1};
    halide_buffer_t computed_bounds{};
    halide_buffer_t buf{};
    halide_buffer_t *tuple = &buf;

    explicit Entry(uint8_t key)
        : key(key) {
        computed_bounds.type = halide_type_t(halide_type_uint, 8);
        computed_bounds.dimensions = 1;
        computed_bounds.dim = &computed_dim;
        buf.type = computed_bounds.type;
        buf.dimensions = 1;
        buf.dim = &dim;
    }

    int lookup() {
        return halide_memoization_cache_lookup(nullptr, &key, 1, &computed_bounds, 1, &tuple);
    }

    int store() {
        return halide_memoization_cache_store(nullptr, &key, 1, &computed_bounds, 1, &tuple, false, 0);
    }

    void release() {
        halide_memoization_cache_release(nullptr, buf.host);
    }

    CacheEntry *cache_entry() {
        return get_pointer_to_header(buf.host)->entry;
    }
};

}  // namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // Pruning skips entries that are in use, so an entry can be pruned while
    // a less recently used neighbor survives it. That neighbor must then link
    // to the entry on the far side of the pruned one.
    {
        halide_memoization_cache_set_size(3 * entry_size);

        Entry a(1), b(2), c(3);
        Entry *entries[] = {&a, &b, &c};
        for (Entry *e : entries) {
            HALIDE_CHECK(user_context, e->lookup() == 1);
            HALIDE_CHECK(user_context, e->store() == 0);
        }
        CacheEntry *entry_a = a.cache_entry();
        CacheEntry *entry_c = c.cache_entry();

        // a remains in use, so this prunes b.
        b.release();
        c.release();
        halide_memoization_cache_set_size(2 * entry_size);

        HALIDE_CHECK(user_context, least_recently_used == entry_a);
        HALIDE_CHECK(user_context, most_recently_used == entry_c);
        HALIDE_CHECK(user_context, entry_a->more_recent == entry_c);
        HALIDE_CHECK(user_context, entry_c->less_recent == entry_a);

        // A hit on a moves it to the most recently used position.
        HALIDE_CHECK(user_context, a.lookup() == 0);
        HALIDE_CHECK(user_context, most_recently_used == entry_a);
        HALIDE_CHECK(user_context, least_recently_used == entry_c);

        // Everything can be pruned once it's no longer in use.
        a.release();
        a.release();
        halide_memoization_cache_set_size(1);
        HALIDE_CHECK(user_context, least_recently_used == nullptr);
        HALIDE_CHECK(user_context, most_recently_used == nullptr);
    }

    halide_memoization_cache_cleanup();
    HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);

    print(user_context) << "Success!\n";
    return 0;
}
