// This header defines a several methods useful for debugging programs that
// operate on the Image class supporting images with arbitrary dimensions.
//
//   Image<uint16_t> input = load_image(argv[1]);
//   input.info("input");  // Output the Image header info
//   input.dump("input");  // Dump the Image data
//   input.stats("input"); // Collect statistics on the Image
//
// These can also be called through macros which automatically tag the
// output with the symbol name used in the program:
//
//   Image_info(input);    // Output the Image header info
//   Image_dump(input);    // Dump the Image data
//   Image_stats(input);   // Collect statistics on the Image
//
// Info can also be output when an Image is initialized without making any
// modifications the program by compiling with -DHL_MEMINIT
//
// These features are off by default.  To enable, compile with the following
// flags:
//
//   -DHL_MEMINFO  Produce Image info output
//   -DHL_MEMINIT  Produce Image info when Image is first initialized
//
#ifndef HALIDE_TOOLS_IMAGE_INFO_H
#define HALIDE_TOOLS_IMAGE_INFO_H

#ifndef HL_MEMINFO
#define Image_info(img)
#define Image_dump(img)
#define Image_stats(img)
#else
// Stringifying macros to automatically fill in the Image info tag
#define Image_info(img)  img.info(#img)
#define Image_dump(img)  img.dump(#img)
#define Image_stats(img) img.stats(#img)
#endif // HL_MEMINFO

#if defined(HL_MEMINFO) || defined(HL_MEMINIT)
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdint.h>  // <cstdint> requires C++11

#include "HalideRuntime.h"

namespace Halide { namespace Tools {

static inline void print_dimid(int d, int val) {
    static const char *dimid[] = {"x", "y", "z", "w"};
    int numdimid = 4;
    if (d < numdimid)
        std::cout << " " << dimid[d] << ":" << val;
    else
        std::cout << " extent[" << d << "]:" << val;
}

static inline void print_loc(int32_t *loc, int dim, int32_t *min) {
    for (int d = 0; d < dim; d++) {
        if (d) std::cout << ",";
        std::cout << loc[d] + min[d];
    }
}

static inline void print_memalign(intptr_t val) {
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

template<typename T>
void Image<T>::info(const char *tag) const {
    int32_t *min = contents->buf.min;
    int32_t *extent = contents->buf.extent;
    int32_t *stride = contents->buf.stride;
    int dim = dimensions();
    int bpp = contents->buf.elem_size;
    int32_t size = 1;

    std::cout << std::endl
              << "-----------------------------------------------------------------------------";
    std::cout << std::endl << "Image info: " << tag
              << " dim:" << dim << " bpp:" << bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }
    std::cout << std::endl;
    std::cout << tag << " class       = 0x" << std::left << std::setw(10) << (void*)this
                     << std::right << " # ";
    print_memalign((intptr_t)this); std::cout << std::endl;
    std::cout << tag << " class size  = "<< sizeof(this)
                     << " (0x"<< std::hex << sizeof(this) << std::dec <<")\n";
    std::cout << tag << "-class => [ 0x" << (void*)this
                     << ", 0x" << (void*)(((char*)this)+sizeof(this)-1)
                     << " ], # size:" << sizeof(this) << ", ";
    print_memalign((intptr_t)this); std::cout << std::endl;
    int img_dim = dimensions();
    int img_bpp = sizeof(T);
    std::cout << tag << " img_dim     = " << img_dim << std::endl;
    std::cout << tag << " bytes/pix   = " << img_bpp << std::endl;
    std::cout << tag << " width       = " << width() << std::endl;
    std::cout << tag << " height      = " << height() << std::endl;
    std::cout << tag << " channels    = " << channels() << std::endl;
    std::cout << tag << " extent[]    = ";
    for (int d = 0; d < dim; d++) {
        std::cout << extent[d] << " ";
    }
    std::cout << std::endl;
    std::cout << tag << " min[]       = ";
    for (int d = 0; d < dim; d++) {
        std::cout << min[d] << " ";
    }
    std::cout << std::endl;
    std::cout << tag << " stride[]    = ";
    for (int d = 0; d < dim; d++) {
        std::cout << stride[d] << " ";
    }
    std::cout << std::endl;
    if (img_bpp > 1) {
        for (int d = 0; d < dim; d++) {
            std::cout << tag << " str[" << d << "]*bpp  = "
                             << std::left << std::setw(12) << stride[d] * img_bpp
                             << std::right << " # ";
            print_memalign(stride[d] * img_bpp); std::cout << std::endl;
        }
    }

    uint8_t *alloc = contents->alloc;
    const T *img_data = data();
    const T *img_next = img_data + size;
    int32_t img_size = size * img_bpp;
    int32_t data_size = (char*)img_next - (char*)img_data;
    std::cout << tag << " size        = " << size << " (0x"
                             << std::hex << size << ")" << std::dec << std::endl;
    std::cout << tag << " img_size    = " << img_size << " (0x"
                             << std::hex << img_size << ")" << std::dec << std::endl;
    std::cout << tag << " data        = 0x" << std::left << std::setw(10) << (void *)img_data
                     << std::right << " # ";
    print_memalign((intptr_t)img_data); std::cout << std::endl;
    std::cout << tag << " next        = 0x" << std::left << std::setw(10) << (void *)img_next
                     << std::right << " # ";
    print_memalign((intptr_t)img_next); std::cout << std::endl;
    std::cout << tag << " alloc       = 0x" << std::left << std::setw(10) << (void *)alloc
                     << std::right << " # ";
    print_memalign((intptr_t)alloc); std::cout << std::endl;
    std::cout << tag << " data_size   = " << data_size  << " (0x"
                             << std::hex << data_size  << ")" << std::dec << std::endl;
    std::cout << tag << " => [ 0x" << (void *)img_data
                         << ", 0x" << (void *)(((char*)img_next)-1)
                         << "], # size:" << data_size << ", ";
    print_memalign((intptr_t)img_data); std::cout << std::endl;
    uint8_t *img_headend = (alloc == (uint8_t *)img_data) ? alloc : (uint8_t*)img_data - 1;
    std::cout << tag << "-header => [ 0x" << (void *)alloc
                         << ", 0x" << (void *)(img_headend)
                         << "], # size:" << (char*)img_data - (char*)alloc << ", ";
    print_memalign((intptr_t)alloc); std::cout << std::endl;
}

template<typename T>
void Image<T>::dump(const char *tag) const {
    int32_t *min = contents->buf.min;
    int32_t *extent = contents->buf.extent;
    int32_t *stride = contents->buf.stride;
    int dim = dimensions();
    int bpp = contents->buf.elem_size;
    int32_t size = 1;

    std::cout << std::endl << "Image dump: " << tag
              << " dim:" << dim << " bpp:" << bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }

    // Arbitrary dimension image traversal
    const T *ptr = (const T *)contents->buf.host;
    int32_t curloc[dim];
    for (int d = 1; d < dim; d++) {
        curloc[d] = -1;
    }
    curloc[0] = 0;

    for (int32_t i = 0; i < size; i++) {
        // Track changes in position in higher dimensions
        for (int d = 1; d < dim; d++) {
            if ((i % stride[d]) == 0) {
                curloc[d]++;
                for (int din = 0; din < d; din++) {
                    curloc[din] = 0;
                }
                std::cout << std::endl;
                // Print separators for dimensions beyond (x0,y1)
                if (d > 1) {
                    print_dimid(d, curloc[d]+min[d]);
                    std::cout << "\n==========================================";
                }
            }
        }

        // Check for start of row (or wrap due to width)
        if ((curloc[0] % 16) == 0) {
            int widx = 0;
            std::ostringstream idx;
            if (dim > 1) {   // Multi-dim, just report (x0,y1) on each row
               idx << "(" << curloc[0]+min[0] << "," << curloc[1]+min[1] << ")";
               widx = 12;
            } else {         // Single-dim
               idx << curloc[0]+min[0];
               widx = 4;
            }
            std::cout << std::endl << std::setw(widx) << idx.str() << ": ";
        }

        // Display data
        std::cout << std::setw(4) << *ptr++ + 0 << " ";

        curloc[0]++;  // Track position in row
    }
    std::cout << std::endl;
}

template<typename T>
void Image<T>::stats(const char *tag) const {
    int32_t *min = contents->buf.min;
    int32_t *extent = contents->buf.extent;
    int32_t *stride = contents->buf.stride;
    int dim = dimensions();
    int bpp = contents->buf.elem_size;
    int32_t size = 1;
    std::cout << std::endl << "Image stats: " << tag
              << " dim:" << dim << " bpp:" << bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }

    // Arbitrary dimension image traversal
    const T *ptr = (const T *)contents->buf.host;
    int32_t curloc[dim];
    for (int d = 1; d < dim; d++) {
        curloc[d] = -1;
    }
    curloc[0] = 0;

    // Statistics
    int32_t cnt = 0;
    double sum = 0;
    T minval = *ptr;
    T maxval = *ptr;
    int32_t minloc[dim];
    int32_t maxloc[dim];
    for (int d = 0; d < dim; d++) {
        minloc[d] = 0;
        maxloc[d] = 0;
    }

    for (int32_t i = 0; i < size; i++) {
        // Track changes in position in higher dimensions
        for (int d = 1; d < dim; d++) {
            if ((i % stride[d]) == 0) {
                curloc[d]++;
                for (int din = 0; din < d; din++) {
                    curloc[din] = 0;
                }
            }
        }

        // Collect data
        T val = *ptr++;
        sum += val;
        cnt++;
        if (val < minval) {
            minval = val;
            for (int d = 0; d < dim; d++) {
                minloc[d] = curloc[d];
            }
        }
        if (val > maxval) {
            maxval = val;
            for (int d = 0; d < dim; d++) {
                maxloc[d] = curloc[d];
            }
        }

        curloc[0]++;  // Track position in row
    }

    double avg = sum / cnt;
    std::cout << std::endl;
    std::cout << "min        = " << minval + 0 << " @ (";
    print_loc(minloc, dim, min);
    std::cout << ")" << std::endl;
    std::cout << "max        = " << maxval + 0 << " @ (";
    print_loc(maxloc, dim, min);
    std::cout << ")" << std::endl;
    std::cout << "mean       = " << avg << std::endl;
    std::cout << "N          = " << cnt << std::endl;
    std::cout << std::endl;
}

}} // namespace Halide::Tools

#else  // ! (HL_MEMINFO || HL_MEMINIT)
namespace Halide { namespace Tools {

template<typename T>
void Image<T>::info(const char *tag) const { }
template<typename T>
void Image<T>::dump(const char *tag) const { }
template<typename T>
void Image<T>::stats(const char *tag) const { }

}} // namespace Halide::Tools
#endif  // HL_MEMINFO || HL_MEMINIT
#endif  // HALIDE_TOOLS_IMAGE_INFO_H
