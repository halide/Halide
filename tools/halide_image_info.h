// This header defines several methods useful for debugging programs that
// operate on the Buffer class supporting images with arbitrary dimensions.
//
//   Buffer<uint16_t> input = load_image(argv[1]);
//
//   info(input, "input");  // Output the Buffer header info
//   dump(input, "input");  // Dump the Buffer data
//   stats(input, "input"); // Report statistics for the Buffer
//
//
#ifndef HALIDE_TOOLS_IMAGE_INFO_H
#define HALIDE_TOOLS_IMAGE_INFO_H

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdint.h>
#include <vector>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

namespace Halide {
namespace Tools {

static inline void print_dimid(int d, int val) {
    static const char *dimid[] = {"x", "y", "z", "w"};
    int numdimid = 4;
    if (d < numdimid) {
        std::cout << " " << dimid[d] << ":" << val;
    } else {
        std::cout << " extent[" << d << "]:" << val;
    }
}

static inline void print_loc(const std::vector<int32_t> &loc, int dim, const int32_t *min) {
    for (int d = 0; d < dim; d++) {
        if (d) {
            std::cout << ",";
        }
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
void info(Runtime::Buffer<T> &img, const char *tag = "Buffer") {
    buffer_t *buf = &(*img);
    int32_t *min = buf->min;
    int32_t *extent = buf->extent;
    int32_t *stride = buf->stride;
    int dim = img.dimensions();
    int img_bpp = buf->elem_size;
    int img_tsize = sizeof(T);
    int img_csize = sizeof(Runtime::Buffer<T>);
    int img_bsize = sizeof(buffer_t);
    int32_t size = 1;
    uint64_t dev = buf->dev;
    bool host_dirty = buf->host_dirty;
    bool dev_dirty = buf->dev_dirty;

    std::cout << std::endl
              << "-----------------------------------------------------------------------------";
    std::cout << std::endl << "Buffer info: " << tag
              << " dim:" << dim << " bpp:" << img_bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }
    std::cout << std::endl;
    std::cout << tag << " class       = 0x" << std::left << std::setw(10) << (void*)img
                     << std::right << " # ";
    print_memalign((intptr_t)&img); std::cout << std::endl;
    std::cout << tag << " class size  = "<< img_csize
                     << " (0x"<< std::hex << img_csize << std::dec <<")\n";
    std::cout << tag << "-class => [ 0x" << (void*)&img
                     << ", 0x" << (void*)(((char*)&img)+img_csize-1)
                     << " ], # size:" << img_csize << ", ";
    print_memalign((intptr_t)&img); std::cout << std::endl;
    std::cout << tag << " buf_t size  = "<< img_bsize
                     << " (0x"<< std::hex << img_bsize << std::dec <<")\n";
    std::cout << tag << "-buf_t => [ 0x" << (void*)&buf
                     << ", 0x" << (void*)(((char*)&buf)+img_bsize-1)
                     << " ], # size:" << img_bsize << ", ";
    print_memalign((intptr_t)&buf); std::cout << std::endl;
    if (img_bpp != img_tsize) {
        std::cout << tag << " sizeof(T)   = " << img_tsize << std::endl;
    }
    std::cout << tag << " host_dirty  = " << host_dirty << std::endl;
    std::cout << tag << " dev_dirty   = " << dev_dirty << std::endl;
    std::cout << tag << " dev handle  = " << dev << std::endl;
    std::cout << tag << " elem_size   = " << img_bpp << std::endl;
    std::cout << tag << " img_dim     = " << dim << std::endl;
    std::cout << tag << " width       = " << img.width() << std::endl;
    std::cout << tag << " height      = " << img.height() << std::endl;
    std::cout << tag << " channels    = " << img.channels() << std::endl;
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

    const T *img_data = img.data();
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
    std::cout << tag << " data_size   = " << data_size  << " (0x"
                             << std::hex << data_size  << ")" << std::dec << std::endl;
    std::cout << tag << " => [ 0x" << (void *)img_data
                         << ", 0x" << (void *)(((char*)img_next)-1)
                         << "], # size:" << data_size << ", ";
    print_memalign((intptr_t)img_data); std::cout << std::endl;
}

template<typename T>
void dump(Runtime::Buffer<T> &img, const char *tag = "Buffer") {
    buffer_t *buf = &(*img);
    int32_t *min = buf->min;
    int32_t *extent = buf->extent;
    int32_t *stride = buf->stride;
    int dim = img.dimensions();
    int bpp = buf->elem_size;
    int32_t size = 1;

    std::cout << std::endl << "Buffer dump: " << tag
              << " dim:" << dim << " bpp:" << bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }

    // Arbitrary dimension image traversal
    const T *ptr = img.data();
    std::vector<int32_t> curloc(dim);
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
void stats(Runtime::Buffer<T> &img, const char *tag = "Buffer") {
    buffer_t *buf = &(*img);
    int32_t *min = buf->min;
    int32_t *extent = buf->extent;
    int32_t *stride = buf->stride;
    int dim = img.dimensions();
    int bpp = buf->elem_size;
    int32_t size = 1;
    std::cout << std::endl << "Buffer stats: " << tag
              << " dim:" << dim << " bpp:" << bpp;
    for (int d = 0; d < dim; d++) {
        print_dimid(d, extent[d]);
        size *= extent[d];
    }

    // Arbitrary dimension image traversal
    const T *ptr = img.data();
    std::vector<int32_t> curloc(dim);
    for (int d = 1; d < dim; d++) {
        curloc[d] = -1;
    }
    curloc[0] = 0;

    // Statistics
    int32_t cnt = 0;
    double sum = 0;
    T minval = *ptr;
    T maxval = *ptr;
    std::vector<int32_t> minloc(dim);
    std::vector<int32_t> maxloc(dim);
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

} // namespace Tools
} // namespace Halide

#endif  // HALIDE_TOOLS_IMAGE_INFO_H
