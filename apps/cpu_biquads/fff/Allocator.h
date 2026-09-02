#ifndef SUPERBLURS_ALLOCATOR_H
#define SUPERBLURS_ALLOCATOR_H

#include "Util.h"
#include <assert.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#define DEBUG_ALLOCATOR 0

static bool debug_allocator() {
    static bool d = getenv("DEBUG_ALLOCATOR") != nullptr;
    return d;
}

struct FreeMemory {
    float *ptr = nullptr;
    size_t width, height, stride = 0;
};

#define MAX_THREADS 256
static std::vector<FreeMemory> *free_pools[MAX_THREADS] = {};

std::vector<FreeMemory> &get_free_pool() {
    static std::atomic<int> num_free_pools{0};
    static thread_local std::vector<FreeMemory> *free_pool = nullptr;

    if (!free_pool) {
        int p = num_free_pools++;
        assert(p < MAX_THREADS);
        free_pool = new std::vector<FreeMemory>;
        free_pools[p] = free_pool;
    }
    return *free_pool;
}

void release_all_memory() {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!free_pools[i]) {
            return;
        }

        for (auto &p : *free_pools[i]) {
            free(p.ptr);
        }
        delete free_pools[i];
        free_pools[i] = nullptr;
    }
}

struct Alloc2D {
    float *ptr = nullptr;
    size_t width = 0, height = 0, stride = 0;
    const char *purpose = nullptr;

    ~Alloc2D() {
        if (!ptr) {
            return;
        }

        auto &free_pool = get_free_pool();

        for (auto &p : free_pool) {
            if (ptr + width == p.ptr && stride == p.stride) {
                // Attach to the left
                p.ptr = ptr;
                p.width += width;
                return;
            }
            if (ptr + stride * height == p.ptr && stride == p.stride) {
                // Attach to the top
                p.ptr = ptr;
                p.height += height;
                return;
            }
        }
        if (debug_allocator()) {
            fprintf(stderr, "Returning memory at %p on thread %d used for %s\n", ptr, thread_id, purpose);
        }
        free_pool.emplace_back(FreeMemory{ptr, width, height, stride});
    }

    Alloc2D() = default;

    Alloc2D(const Alloc2D &) = delete;
    Alloc2D &operator=(const Alloc2D &) = delete;
    Alloc2D(Alloc2D &&other)
        : ptr(other.ptr), width(other.width), height(other.height), stride(other.stride), purpose(other.purpose) {
        other.ptr = nullptr;
    }
    Alloc2D &operator=(Alloc2D &&other) {
        assert(ptr == nullptr);
        ptr = other.ptr;
        width = other.width;
        height = other.height;
        stride = other.stride;
        purpose = other.purpose;
        other.ptr = nullptr;
        return *this;
    }

    Alloc2D(size_t width, size_t height, const char *purpose)
        : width(width), height(height), purpose(purpose) {
        auto &free_pool = get_free_pool();
        for (auto &p : free_pool) {
            // First-fit
            if (p.width >= width && p.height >= height) {
                // Either take the bottom of it or the right of it
                ptr = p.ptr;

                if (p.height - height > p.stride - stride) {
                    // Claim the top of it
                    p.ptr += p.stride * height;
                    p.height -= height;
                } else {
                    // Claim the left of it
                    p.ptr += width;
                    p.width -= width;
                }
                stride = p.stride;
                if (debug_allocator()) {
                    fprintf(stderr, "Borrowing memory at %p %zu %zu %zu -> %p for %s\n", ptr, width, height, stride, p.ptr, purpose);
                }
                break;
            }
        }
        if (!ptr) {
            stride = (width + 15) & ~15;
            ptr = (float *)aligned_alloc(64, stride * height * sizeof(float));
            if (debug_allocator()) {
                fprintf(stderr, "Allocating memory at %p %zu %zu %zu for on thread %d %s\n", ptr, width, height, stride, thread_id, purpose);

                // Fill with garbage for debugging use of uninitialized values
                global_mutex.lock();
                fprintf(stderr, "Filling allocation with garbage for debugging\n");
                global_mutex.unlock();
                for (size_t y = 0; y < height; y++) {
                    for (size_t x = 0; x < width; x++) {
                        ptr[y * stride + x] = (x + y) & 3;
                    }
                }
            }
        }
    }
};

// An RAII object that temporarily lets go of some memory so that it can be used
// (and clobbered) by any code within the scope of the object. It is guaranteed
// that the memory is good to use again after this object goes out of scope.
struct ScratchSpace {
    float *ptr = nullptr;

    ScratchSpace(float *ptr, size_t width, size_t height, size_t stride)
        : ptr(ptr) {
        if (ptr) {
            if (debug_allocator()) {
                fprintf(stderr, "Loaning out memory at %p on thread %d\n", ptr, thread_id);
            }
            get_free_pool().emplace_back(FreeMemory{ptr, width, height, stride});
        }
    }

    ~ScratchSpace() {
        if (!ptr) {
            return;
        }
        // Reclaim this memory. It had better be in the borrowable pool in the
        // state we left it in.
        auto &free_pool = get_free_pool();
        for (auto it = free_pool.begin(); it != free_pool.end(); it++) {
            if (it->ptr == ptr) {
                if (debug_allocator()) {
                    fprintf(stderr, "Reacquiring memory at %p on thread %d\n", ptr, thread_id);
                }
                free_pool.erase(it);
                return;
            }
        }
        assert(false && "Borrowed memory was never returned");
    }
};

#endif
