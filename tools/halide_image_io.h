// This simple PNG IO library works with *both* the Halide::Image<T> type *and*
// the simple halide_image.h version. Also now includes PPM support for faster load/save.

#ifndef HALIDE_IMAGE_IO_H
#define HALIDE_IMAGE_IO_H

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef HALIDE_NOPNG
#include "png.h"
#endif

namespace Halide {
namespace Tools {

namespace Internal {

typedef bool (*CheckFunc)(bool condition, const char* fmt, ...);

inline bool CheckFail(bool condition, const char* fmt, ...) {
    if (!condition) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        fprintf(stderr, "%s", buffer);
        exit(-1);
    }
    return condition;
}

inline bool CheckReturn(bool condition, const char* fmt, ...) {
    return condition;
}

// Convert to u8
inline void convert(uint8_t in, uint8_t &out) {out = in;}
inline void convert(uint16_t in, uint8_t &out) {out = in >> 8;}
inline void convert(uint32_t in, uint8_t &out) {out = in >> 24;}
inline void convert(int8_t in, uint8_t &out) {out = in;}
inline void convert(int16_t in, uint8_t &out) {out = in >> 8;}
inline void convert(int32_t in, uint8_t &out) {out = in >> 24;}
inline void convert(float in, uint8_t &out) {out = (uint8_t)(in*255.0f);}
inline void convert(double in, uint8_t &out) {out = (uint8_t)(in*255.0f);}

// Convert to u16
inline void convert(uint8_t in, uint16_t &out) {out = in << 8;}
inline void convert(uint16_t in, uint16_t &out) {out = in;}
inline void convert(uint32_t in, uint16_t &out) {out = in >> 16;}
inline void convert(int8_t in, uint16_t &out) {out = in << 8;}
inline void convert(int16_t in, uint16_t &out) {out = in;}
inline void convert(int32_t in, uint16_t &out) {out = in >> 16;}
inline void convert(float in, uint16_t &out) {out = (uint16_t)(in*65535.0f);}
inline void convert(double in, uint16_t &out) {out = (uint16_t)(in*65535.0f);}

// Convert from u8
inline void convert(uint8_t in, uint32_t &out) {out = in << 24;}
inline void convert(uint8_t in, int8_t &out) {out = in;}
inline void convert(uint8_t in, int16_t &out) {out = in << 8;}
inline void convert(uint8_t in, int32_t &out) {out = in << 24;}
inline void convert(uint8_t in, float &out) {out = in/255.0f;}
inline void convert(uint8_t in, double &out) {out = in/255.0f;}

// Convert from u16
inline void convert(uint16_t in, uint32_t &out) {out = in << 16;}
inline void convert(uint16_t in, int8_t &out) {out = in >> 8;}
inline void convert(uint16_t in, int16_t &out) {out = in;}
inline void convert(uint16_t in, int32_t &out) {out = in << 16;}
inline void convert(uint16_t in, float &out) {out = in/65535.0f;}
inline void convert(uint16_t in, double &out) {out = in/65535.0f;}


inline bool ends_with_ignore_case(const std::string &ac, const std::string &bc) {
    if (ac.length() < bc.length()) { return false; }
    std::string a = ac, b = bc;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a.compare(a.length()-b.length(), b.length(), b) == 0;
}

inline bool is_little_endian() {
    int value = 1;
    return ((char *) &value)[0] == 1;
}

inline void swap_endian_16(bool little_endian, uint16_t &value) {
    if (little_endian) {
        value = ((value & 0xff)<<8)|((value & 0xff00)>>8);
    }
}

struct FileOpener {
    FileOpener(const char* filename, const char* mode) : f(fopen(filename, mode)) {
        // nothing
    }
    ~FileOpener() {
        if (f != nullptr) {
            fclose(f);
        }
    }
    // read a line of data skipping lines that begin with '#"
    char *readLine(char *buf, int maxlen) {
        char *status;
        do {
            status = fgets(buf, maxlen, f);
        } while(status && buf[0] == '#');
        return(status);
    }
    FILE * const f;
};

#ifndef HALIDE_NOPNG
struct PngRowPointers {
    PngRowPointers(int height, int rowbytes) : p(new png_bytep[height]), height(height) {
        if (p != nullptr) {
            for (int y = 0; y < height; y++) {
                p[y] = new png_byte[rowbytes];
            }
        }
    }
    ~PngRowPointers() {
        if (p) {
            for (int y = 0; y < height; y++) {
                delete[] p[y];
            }
            delete[] p;
        }
    }
    png_bytep* const p;
    int const height;
};
#endif // HALIDE_NOPNG

}  // namespace Internal


template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_png(const std::string &filename, ImageType *im) {
#ifdef HALIDE_NOPNG
    return false;
#else // HALIDE_NOPNG
    png_byte header[8];
    png_structp png_ptr;
    png_infop info_ptr;

    /* open file and test for it being a png */
    Internal::FileOpener f(filename.c_str(), "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) return false;
    if (!check(fread(header, 1, 8, f.f) == 8, "File ended before end of header\n")) return false;
    if (!check(!png_sig_cmp(header, 0, 8), "File %s is not recognized as a PNG file\n", filename.c_str())) return false;

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!check(png_ptr != nullptr, "png_create_read_struct failed\n")) return false;

    info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "png_create_info_struct failed\n")) return false;

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error during init_io\n")) return false;

    png_init_io(png_ptr, f.f);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // Expand low-bpp images to have only 1 pixel per byte (As opposed to tight packing)
    if (bit_depth < 8) {
        png_set_packing(png_ptr);
    }

    if (channels != 1) {
        *im = ImageType(width, height, channels);
    } else {
        *im = ImageType(width, height);
    }

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    // read the file
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error during read_image\n")) return false;

    Internal::PngRowPointers row_pointers(im->height(), png_get_rowbytes(png_ptr, info_ptr));
    png_read_image(png_ptr, row_pointers.p);

    if (!check((bit_depth == 8) || (bit_depth == 16), "Can only handle 8-bit or 16-bit pngs\n")) return false;

    // convert the data to ImageType::ElemType

    int c_stride = (im->channels() == 1) ? 0 : im->stride(2);
    typename ImageType::ElemType *ptr = (typename ImageType::ElemType*)im->data();
    if (bit_depth == 8) {
        for (int y = 0; y < im->height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers.p[y]);
            for (int x = 0; x < im->width(); x++) {
                for (int c = 0; c < im->channels(); c++) {
                    Internal::convert(*srcPtr++, ptr[c*c_stride]);
                }
                ptr++;
            }
        }
    } else if (bit_depth == 16) {
        for (int y = 0; y < im->height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers.p[y]);
            for (int x = 0; x < im->width(); x++) {
                for (int c = 0; c < im->channels(); c++) {
                    uint16_t hi = (*srcPtr++) << 8;
                    uint16_t lo = hi | (*srcPtr++);
                    Internal::convert(lo, ptr[c*c_stride]);
                }
                ptr++;
            }
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    im->set_host_dirty();
    return true;
#endif // HALIDE_NOPNG
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_png(ImageType &im, const std::string &filename) {
#ifdef HALIDE_NOPNG
    return false;
#else // HALIDE_NOPNG
    png_structp png_ptr;
    png_infop info_ptr;
    png_byte color_type;

    im.copy_to_host();

    if (!check(im.channels() > 0 && im.channels() < 5,
           "Can't write PNG files that have other than 1, 2, 3, or 4 channels\n")) return false;

    png_byte color_types[4] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
                               PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA
                              };
    color_type = color_types[im.channels() - 1];

    // open file
    Internal::FileOpener f(filename.c_str(), "wb");
    if (!check(f.f != nullptr, "[write_png_file] File %s could not be opened for writing\n", filename.c_str())) return false;

    // initialize stuff
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!check(png_ptr != nullptr, "[write_png_file] png_create_write_struct failed\n")) return false;

    info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "[write_png_file] png_create_info_struct failed\n")) return false;

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during init_io\n")) return false;

    png_init_io(png_ptr, f.f);

    unsigned int bit_depth = 16;
    if (sizeof(typename ImageType::ElemType) == 1) {
        bit_depth = 8;
    }

    // write header
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing header\n")) return false;

    png_set_IHDR(png_ptr, info_ptr, im.width(), im.height(),
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    Internal::PngRowPointers row_pointers(im.height(), png_get_rowbytes(png_ptr, info_ptr));

    // im.copyToHost(); // in case the image is on the gpu

    int c_stride = (im.channels() == 1) ? 0 : im.stride(2);
    typename ImageType::ElemType *srcPtr = (typename ImageType::ElemType*)im.data();

    for (int y = 0; y < im.height(); y++) {
        uint8_t *dstPtr = (uint8_t *)(row_pointers.p[y]);
        if (bit_depth == 16) {
            // convert to uint16_t
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint16_t out;
                    Internal::convert(srcPtr[c*c_stride], out);
                    *dstPtr++ = out >> 8;
                    *dstPtr++ = out & 0xff;
                }
                srcPtr++;
            }
        } else if (bit_depth == 8) {
            // convert to uint8_t
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint8_t out;
                    Internal::convert(srcPtr[c*c_stride], out);
                    *dstPtr++ = out;
                }
                srcPtr++;
            }
        } else {
            if (!check(bit_depth == 8 || bit_depth == 16, "We only support saving 8- and 16-bit images.")) return false;
        }
    }

    // write data
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing bytes")) return false;

    png_write_image(png_ptr, row_pointers.p);

    // finish write
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during end of write")) return false;

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    return true;
#endif // HALIDE_NOPNG
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_pgm(const std::string &filename, ImageType *im) {

    /* open file and test for it being a pgm */
    Internal::FileOpener f(filename.c_str(), "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) return false;

    int width, height, maxval;
    char header[256];
    char buf[1024];
    bool fmt_binary = false;

    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%255s", header) == 1, "Could not read PGM header\n")) return false;
    if (header == std::string("P5") || header == std::string("p5"))
        fmt_binary = true;
    if (!check(fmt_binary, "Input is not binary PGM\n")) return false;

    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%d %d\n", &width, &height) == 2, "Could not read PGM width and height\n")) return false;
    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%d", &maxval) == 1, "Could not read PGM max value\n")) return false;

    int bit_depth = 0;
    if (maxval == 255) { bit_depth = 8; }
    else if (maxval == 65535) { bit_depth = 16; }
    else if (!check(false, "Invalid bit depth in PGM\n")) { return false; }

    // Graymap
    *im = ImageType(width, height);

    // convert the data to ImageType::ElemType
    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height);
        if (!check(fread((void *) &data[0], sizeof(uint8_t), width*height, f.f) == (size_t) (width*height), "Could not read PGM 8-bit data\n")) return false;
        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im->data();
        uint8_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Internal::convert(*p++, *im_data++);
            }
        }
    } else if (bit_depth == 16) {
        int little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height);
        if (!check(fread((void *) &data[0], sizeof(uint16_t), width*height, f.f) == (size_t) (width*height), "Could not read PGM 16-bit data\n")) return false;
        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im->data();
        uint16_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value = *p++;
                Internal::swap_endian_16(little_endian, value);
                Internal::convert(value, *im_data++);
            }
        }
    }
    (*im)(0,0,0) = (*im)(0,0,0);      /* Mark dirty inside read/write functions. */

    return true;
}

// "im" is not const-ref because copy_to_host() is not const.
// Optional channel parameter for specifying which color to save as a graymap
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_pgm(ImageType &im, const std::string &filename, unsigned int channel = 0) {
    im.copy_to_host();

    unsigned int bit_depth = sizeof(typename ImageType::ElemType) == 1 ? 8: 16;
    unsigned int num_channels = im.channels();

    if (!check(channel >= 0, "Selected channel %d not available in image\n", channel)) return false;
    if (!check(channel < num_channels, "Selected channel %d not available in image\n", channel)) return false;
    Internal::FileOpener f(filename.c_str(), "wb");
    if (!check(f.f != nullptr, "File %s could not be opened for writing\n", filename.c_str())) return false;
    fprintf(f.f, "P5\n%d %d\n%d\n", im.width(), im.height(), (1<<bit_depth)-1);
    int width = im.width(), height = im.height();

    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height);
        uint8_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Internal::convert(im(x, y, channel), *p++);
            }
        }
        if (!check(fwrite((void *) &data[0], sizeof(uint8_t), width*height, f.f) == (size_t) (width*height), "Could not write PGM 8-bit data\n")) return false;
    } else if (bit_depth == 16) {
        int little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height);
        uint16_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value;
                Internal::convert(im(x, y, channel), value);
                Internal::swap_endian_16(little_endian, value);
                *p++ = value;
            }
        }
        if (!check(fwrite((void *) &data[0], sizeof(uint16_t), width*height, f.f) == (size_t) (width*height), "Could not write PGM 16-bit data\n")) return false;
    } else {
        return check(false, "We only support saving 8- and 16-bit images.");
    }
    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_ppm(const std::string &filename, ImageType *im) {

    /* open file and test for it being a ppm */
    Internal::FileOpener f(filename.c_str(), "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) return false;

    int width, height, maxval;
    char header[256];
    char buf[1024];
    bool fmt_binary = false;

    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%255s", header) == 1, "Could not read PPM header\n")) return false;
    if (header == std::string("P6") || header == std::string("p6"))
        fmt_binary = true;
    if (!check(fmt_binary, "Input is not binary PPM\n")) return false;

    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%d %d\n", &width, &height) == 2, "Could not read PPM width and height\n")) return false;
    f.readLine(buf, 1024);
    if (!check(sscanf(buf, "%d", &maxval) == 1, "Could not read PPM max value\n")) return false;

    int bit_depth = 0;
    if (maxval == 255) { bit_depth = 8; }
    else if (maxval == 65535) { bit_depth = 16; }
    else if (!check(false, "Invalid bit depth in PPM\n")) { return false; }

    int channels = 3;
    *im = ImageType(width, height, channels);

    // convert the data to ImageType::ElemType
    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height*3);
        if (!check(fread((void *) &data[0], sizeof(uint8_t), width*height*3, f.f) == (size_t) (width*height*3), "Could not read PPM 8-bit data\n")) return false;
        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im->data();
        uint8_t *row = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Internal::convert(*row++, im_data[(0*height+y)*width+x]);
                Internal::convert(*row++, im_data[(1*height+y)*width+x]);
                Internal::convert(*row++, im_data[(2*height+y)*width+x]);
            }
        }
    } else if (bit_depth == 16) {
        int little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height*3);
        if (!check(fread((void *) &data[0], sizeof(uint16_t), width*height*3, f.f) == (size_t) (width*height*3), "Could not read PPM 16-bit data\n")) return false;
        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im->data();
        uint16_t *row = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value;
                value = *row++;
                Internal::swap_endian_16(little_endian, value);
                Internal::convert(value, im_data[(0*height+y)*width+x]);
                value = *row++;
                Internal::swap_endian_16(little_endian, value);
                Internal::convert(value, im_data[(1*height+y)*width+x]);
                value = *row++;
                Internal::swap_endian_16(little_endian, value);
                Internal::convert(value, im_data[(2*height+y)*width+x]);
            }
        }
    }
    (*im)(0,0,0) = (*im)(0,0,0);      /* Mark dirty inside read/write functions. */

    return true;
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_ppm(ImageType &im, const std::string &filename) {
    im.copy_to_host();

    unsigned int bit_depth = sizeof(typename ImageType::ElemType) == 1 ? 8: 16;

    Internal::FileOpener f(filename.c_str(), "wb");
    if (!check(f.f != nullptr, "File %s could not be opened for writing\n", filename.c_str())) return false;
    fprintf(f.f, "P6\n%d %d\n%d\n", im.width(), im.height(), (1<<bit_depth)-1);
    int width = im.width(), height = im.height(), channels = im.channels();

    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height*3);
        uint8_t *p = &data[0];
        // unroll inner loop for 3 channel RGB (common case)
        if (channels == 3) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    Internal::convert(im(x, y, 0), *p++);
                    Internal::convert(im(x, y, 1), *p++);
                    Internal::convert(im(x, y, 2), *p++);
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    for (int c = 0; c < channels; c++) {
                        Internal::convert(im(x, y, c), *p++);
                    }
                }
            }
        }
        if (!check(fwrite((void *) &data[0], sizeof(uint8_t), width*height*3, f.f) == (size_t) (width*height*3), "Could not write PPM 8-bit data\n")) return false;
    } else if (bit_depth == 16) {
        int little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height*3);
        uint16_t *p = &data[0];
        // unroll inner loop for 3 channel RGB (common case)
        if (channels == 3) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint16_t value0, value1, value2;
                    Internal::convert(im(x, y, 0), value0);
                    Internal::swap_endian_16(little_endian, value0);
                    *p++ = value0;
                    Internal::convert(im(x, y, 1), value1);
                    Internal::swap_endian_16(little_endian, value1);
                    *p++ = value1;
                    Internal::convert(im(x, y, 2), value2);
                    Internal::swap_endian_16(little_endian, value2);
                    *p++ = value2;
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    for (int c = 0; c < channels; c++) {
                        uint16_t value;
                        Internal::convert(im(x, y, c), value);
                        Internal::swap_endian_16(little_endian, value);
                        *p++ = value;
                    }
                }
            }
        }
        if (!check(fwrite((void *) &data[0], sizeof(uint16_t), width*height*3, f.f) == (size_t) (width*height*3), "Could not write PPM 16-bit data\n")) return false;
    } else {
        return check(false, "We only support saving 8- and 16-bit images.");
    }
    return true;
}

// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load(const std::string &filename, ImageType *im) {
    if (Internal::ends_with_ignore_case(filename, ".png")) {
        return load_png<ImageType, check>(filename, im);
    } else if (Internal::ends_with_ignore_case(filename, ".pgm")) {
        return load_pgm<ImageType, check>(filename, im);
    } else if (Internal::ends_with_ignore_case(filename, ".ppm")) {
        return load_ppm<ImageType, check>(filename, im);
    } else {
        return check(false, "[load] unsupported file extension (png|pgm|ppm supported)");
    }
}
// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save(ImageType &im, const std::string &filename) {
    if (Internal::ends_with_ignore_case(filename, ".png")) {
        return save_png<ImageType, check>(im, filename);
    } else if (Internal::ends_with_ignore_case(filename, ".pgm")) {
        return save_pgm<ImageType, check>(im, filename);
    } else if (Internal::ends_with_ignore_case(filename, ".ppm")) {
        return save_ppm<ImageType, check>(im, filename);
    } else {
        return check(false, "[save] unsupported file extension (png|pgm|ppm supported)");
    }
}

// Fancy wrapper to call load() with CheckFail, inferring the return type;
// this allows you to simply use
//
//    Image im = load_image("filename");
//
// without bothering to check error results (all errors simply abort).
class load_image {
public:
    load_image(const std::string &f) : filename(f) {}
    template<typename ImageType>
    inline operator ImageType() {
        ImageType im;
        (void) load<ImageType, Internal::CheckFail>(filename, &im);
        return im;
    }
private:
  const std::string filename;
};

// Fancy wrapper to call save() with CheckFail; this allows you to simply use
//
//    save_image(im, "filename");
//
// without bothering to check error results (all errors simply abort).
template<typename ImageType>
void save_image(ImageType &im, const std::string &filename) {
    (void) save<ImageType, Internal::CheckFail>(im, filename);
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_IMAGE_IO_H
