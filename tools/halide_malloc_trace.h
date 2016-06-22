#ifndef HALIDE_MALLOC_TRACE_H
#define HALIDE_MALLOC_TRACE_H

//---------------------------------------------------------------------------
// The custom trace allocator can be used in an application by calling:
//
//   halide_enable_malloc_trace();
//
// When the app is run, calls to halide_malloc/free will produce output like:
//
//   halide_malloc => [0x9e400, 0xa27ff], # size:17408, align:1K
//   halide-header => [0x9e390, 0x9e3ff], # size:112, align:16
//   halide_malloc => [0xa2880, 0xa6e9f], # size:17952, align:128
//   halide-header => [0xa2820, 0xa287f], # size:96, align:32
//   halide_free   => [0x9e390, 0x9e3ff], # size:112, align:16
//   halide_free   => [0xa2820, 0xa287f], # size:96, align:32
//
//---------------------------------------------------------------------------

#include <cstdlib>
#include <memory>
#include <iostream>

namespace Halide {
namespace Tools {

static inline void print_meminfoalign(intptr_t val) {
    intptr_t align_chk = 1024*1024;
    while (align_chk > 0) {
        if ((val & (align_chk-1)) == 0) {
            char aunit = ' ';
            if (align_chk >= 1024) {
                align_chk >>= 10;
                aunit = 'K';
            }
            if (align_chk >= 1024) {
                align_chk >>= 10;
                aunit = 'M';
            }
            std::cout << "align:" << align_chk;
            if (aunit != ' ') {
                std::cout << aunit;
            }
            break;
        }
        align_chk >>= 1;
    }
}

void *halide_malloc_trace(void *user_context, size_t x) {
    // Halide requires halide_malloc to allocate memory that can be
    // read 8 bytes before the start to store the original pointer.
    // Additionally, we also need to align it to the natural vector
    // width.
    void *orig = malloc(x+128);
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    // Round up to next multiple of 128.
    void *ptr = (void *)((((size_t)orig + 128) >> 7) << 7);
    ((void **)ptr)[-1] = orig;

    void *headend = (orig == ptr) ? orig : (char *)ptr - 1;
    std::cout << "halide_malloc => [0x" << std::hex
              << (intptr_t)ptr << ", 0x"
              << (intptr_t)ptr + x-1 << std::dec
              << "], # size:"
              << (intptr_t)x << ", ";
    print_meminfoalign((intptr_t)ptr);
    std::cout << std::endl;

    std::cout << "halide-header => [0x" << std::hex
              << (intptr_t)orig << ", 0x"
              << (intptr_t)headend << std::dec
              << "], # size:"
              << (intptr_t)ptr - (intptr_t)orig << ", ";
    print_meminfoalign((intptr_t)orig);
    std::cout << std::endl;
    return ptr;
}

void halide_free_trace(void *user_context, void *ptr) {
    std::cout << "halide_free => [0x" << std::hex
              << (intptr_t)((void**)ptr)[-1] << ", 0x"
              << (intptr_t)ptr - 1 << std::dec
              << "], # size:"
              << (intptr_t)ptr - (intptr_t)((void**)ptr)[-1] << ", ";
    print_meminfoalign((intptr_t)((void**)ptr)[-1]);
    std::cout << std::endl;
    free(((void**)ptr)[-1]);
}

void halide_enable_malloc_trace(void) {
    halide_set_custom_malloc(halide_malloc_trace);
    halide_set_custom_free(halide_free_trace);
}

} // namespace Tools
} // namespace Halide

#endif // HALIDE_MALLOC_TRACE_H
