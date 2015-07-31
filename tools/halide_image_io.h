// This simple PNG IO library works with *both* the Halide::Image<T> type *and*
// the simple static_image.h version. Also now includes PPM support for faster load/save.
// If you want the static_image.h version, to use in a program statically
// linking against a Halide pipeline pre-compiled with Func::compile_to_file, you
// need to explicitly #include static_image.h first.

#ifndef HALIDE_IMAGE_IO_H
#define HALIDE_IMAGE_IO_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "png.h"

namespace Halide {
namespace Tools {

namespace Internal {

inline void _assert(bool condition, const char* fmt, ...) {
    if (!condition) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        exit(-1);
    }
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


inline bool ends_with_ignore_case(std::string a, std::string b) {
    if (a.length() < b.length()) { return false; }
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a.compare(a.length()-b.length(), b.length(), b) == 0;
}

}  // namespace Internal


template<typename ImageType>
ImageType load_png(std::string filename) {
    png_byte header[8];
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;

    /* open file and test for it being a png */
    FILE *f = fopen(filename.c_str(), "rb");
    Internal::_assert(f, "File %s could not be opened for reading\n", filename.c_str());
    Internal::_assert(fread(header, 1, 8, f) == 8, "File ended before end of header\n");
    Internal::_assert(!png_sig_cmp(header, 0, 8), "File %s is not recognized as a PNG file\n", filename.c_str());

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    Internal::_assert(png_ptr, "png_create_read_struct failed\n");

    info_ptr = png_create_info_struct(png_ptr);
    Internal::_assert(info_ptr, "png_create_info_struct failed\n");

    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "Error during init_io\n");

    png_init_io(png_ptr, f);
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

    ImageType im(1);
    if (channels != 1) {
        im = ImageType(width, height, channels);
    } else {
        im = ImageType(width, height);
    }

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    // read the file
    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "Error during read_image\n");

    row_pointers = new png_bytep[im.height()];
    for (int y = 0; y < im.height(); y++) {
        row_pointers[y] = new png_byte[png_get_rowbytes(png_ptr, info_ptr)];
    }

    png_read_image(png_ptr, row_pointers);

    fclose(f);

    Internal::_assert((bit_depth == 8) || (bit_depth == 16), "Can only handle 8-bit or 16-bit pngs\n");

    // convert the data to ImageType::ElemType

    int c_stride = (im.channels() == 1) ? 0 : im.stride(2);
    typename ImageType::ElemType *ptr = (typename ImageType::ElemType*)im.data();
    if (bit_depth == 8) {
        for (int y = 0; y < im.height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers[y]);
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    Internal::convert(*srcPtr++, ptr[c*c_stride]);
                }
                ptr++;
            }
        }
    } else if (bit_depth == 16) {
        for (int y = 0; y < im.height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers[y]);
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint16_t hi = (*srcPtr++) << 8;
                    uint16_t lo = hi | (*srcPtr++);
                    Internal::convert(lo, ptr[c*c_stride]);
                }
                ptr++;
            }
        }
    }

    // clean up
    for (int y = 0; y < im.height(); y++) {
        delete[] row_pointers[y];
    }
    delete[] row_pointers;

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    im.set_host_dirty();
    return im;
}

template<typename ImageType>
void save_png(ImageType im, std::string filename) {
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    png_byte color_type;

    im.copy_to_host();

    Internal::_assert(im.channels() > 0 && im.channels() < 5,
           "Can't write PNG files that have other than 1, 2, 3, or 4 channels\n");

    png_byte color_types[4] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
                               PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA
                              };
    color_type = color_types[im.channels() - 1];

    // open file
    FILE *f = fopen(filename.c_str(), "wb");
    Internal::_assert(f, "[write_png_file] File %s could not be opened for writing\n", filename.c_str());

    // initialize stuff
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    Internal::_assert(png_ptr, "[write_png_file] png_create_write_struct failed\n");

    info_ptr = png_create_info_struct(png_ptr);
    Internal::_assert(info_ptr, "[write_png_file] png_create_info_struct failed\n");

    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during init_io\n");

    png_init_io(png_ptr, f);

    unsigned int bit_depth = 16;
    if (sizeof(typename ImageType::ElemType) == 1) {
        bit_depth = 8;
    }

    // write header
    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing header\n");

    png_set_IHDR(png_ptr, info_ptr, im.width(), im.height(),
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    row_pointers = new png_bytep[im.height()];

    // im.copyToHost(); // in case the image is on the gpu

    int c_stride = (im.channels() == 1) ? 0 : im.stride(2);
    typename ImageType::ElemType *srcPtr = (typename ImageType::ElemType*)im.data();

    for (int y = 0; y < im.height(); y++) {
        row_pointers[y] = new png_byte[png_get_rowbytes(png_ptr, info_ptr)];
        uint8_t *dstPtr = (uint8_t *)(row_pointers[y]);
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
            Internal::_assert(bit_depth == 8 || bit_depth == 16, "We only support saving 8- and 16-bit images.");
        }
    }

    // write data
    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing bytes");

    png_write_image(png_ptr, row_pointers);

    // finish write
    Internal::_assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during end of write");

    png_write_end(png_ptr, NULL);

    // clean up
    for (int y = 0; y < im.height(); y++) {
        delete[] row_pointers[y];
    }
    delete[] row_pointers;

    fclose(f);

    png_destroy_write_struct(&png_ptr, &info_ptr);
}



inline int is_little_endian() {
    int value = 1;
    return ((char *) &value)[0] == 1;
}

#define SWAP_ENDIAN16(little_endian, value) if (little_endian) { (value) = (((value) & 0xff)<<8)|(((value) & 0xff00)>>8); }

template<typename ImageType>
ImageType load_ppm(std::string filename) {

    /* open file and test for it being a ppm */
    FILE *f = fopen(filename.c_str(), "rb");
    Internal::_assert(f, "File %s could not be opened for reading\n", filename.c_str());

    int width, height, maxval;
    char header[256];
    Internal::_assert(fscanf(f, "%255s", header) == 1, "Could not read PPM header\n");
    Internal::_assert(fscanf(f, "%d %d\n", &width, &height) == 2, "Could not read PPM width and height\n");
    Internal::_assert(fscanf(f, "%d", &maxval) == 1, "Could not read PPM max value\n");
    Internal::_assert(fgetc(f) != EOF, "Could not read char from PPM\n");

    int bit_depth = 0;
    if (maxval == 255) { bit_depth = 8; }
    else if (maxval == 65535) { bit_depth = 16; }
    else { Internal::_assert(false, "Invalid bit depth in PPM\n"); }

    Internal::_assert(header == std::string("P6") || header == std::string("p6"), "Input is not binary PPM\n");

    int channels = 3;
    ImageType im(width, height, channels);

    // convert the data to ImageType::ElemType
    if (bit_depth == 8) {
        uint8_t *data = new uint8_t[width*height*3];
        Internal::_assert(fread((void *) data,
                      sizeof(uint8_t), width*height*3, f) == (size_t) (width*height*3),
                "Could not read PPM 8-bit data\n");
        fclose(f);

        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im.data();
        for (int y = 0; y < im.height(); y++) {
            uint8_t *row = (uint8_t *)(&data[(y*width)*3]);
            for (int x = 0; x < im.width(); x++) {
                Internal::convert(*row++, im_data[(0*height+y)*width+x]);
                Internal::convert(*row++, im_data[(1*height+y)*width+x]);
                Internal::convert(*row++, im_data[(2*height+y)*width+x]);
            }
        }
        delete[] data;
    } else if (bit_depth == 16) {
        int little_endian = is_little_endian();
        uint16_t *data = new uint16_t[width*height*3];
        Internal::_assert(fread((void *) data, sizeof(uint16_t), width*height*3, f) == (size_t) (width*height*3), "Could not read PPM 16-bit data\n");
        fclose(f);
        typename ImageType::ElemType *im_data = (typename ImageType::ElemType*) im.data();
        for (int y = 0; y < im.height(); y++) {
            uint16_t *row = (uint16_t *) (&data[(y*width)*3]);
            for (int x = 0; x < im.width(); x++) {
                uint16_t value;
                value = *row++; SWAP_ENDIAN16(little_endian, value); Internal::convert(value, im_data[(0*height+y)*width+x]);
                value = *row++; SWAP_ENDIAN16(little_endian, value); Internal::convert(value, im_data[(1*height+y)*width+x]);
                value = *row++; SWAP_ENDIAN16(little_endian, value); Internal::convert(value, im_data[(2*height+y)*width+x]);
            }
        }
        delete[] data;
    }
    im(0,0,0) = im(0,0,0);      /* Mark dirty inside read/write functions. */

    return im;
}

template<typename ImageType>
void save_ppm(ImageType im, std::string filename) {
	unsigned int bit_depth = sizeof(typename ImageType::ElemType) == 1 ? 8: 16;

    FILE *f = fopen(filename.c_str(), "wb");
    Internal::_assert(f, "File %s could not be opened for writing\n", filename.c_str());
    fprintf(f, "P6\n%d %d\n%d\n", im.width(), im.height(), (1<<bit_depth)-1);
    int width = im.width(), height = im.height();

    if (bit_depth == 8) {
        uint8_t *data = new uint8_t[width*height*3];
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                uint8_t *p = (uint8_t *)(&data[(y*width+x)*3]);
                for (int c = 0; c < im.channels(); c++) {
                    Internal::convert(im(x, y, c), p[c]);
                }
            }
        }
        Internal::_assert(fwrite((void *) data, sizeof(uint8_t), width*height*3, f) == (size_t) (width*height*3), "Could not write PPM 8-bit data\n");
        delete[] data;
    } else if (bit_depth == 16) {
        int little_endian = is_little_endian();
        uint16_t *data = new uint16_t[width*height*3];
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                uint16_t *p = (uint16_t *)(&data[(y*width+x)*3]);
                for (int c = 0; c < im.channels(); c++) {
                    uint16_t value;
                    Internal::convert(im(x, y, c), value);
                    SWAP_ENDIAN16(little_endian, value);
                    p[c] = value;
                }
            }
        }
        Internal::_assert(fwrite((void *) data, sizeof(uint16_t), width*height*3, f) == (size_t) (width*height*3), "Could not write PPM 16-bit data\n");
        delete[] data;
    }
    fclose(f);
}

template<typename ImageType>
ImageType load(std::string filename) {
    if (Internal::ends_with_ignore_case(filename, ".png")) {
        return load_png<ImageType>(filename);
    } else if (Internal::ends_with_ignore_case(filename, ".ppm")) {
        return load_ppm<ImageType>(filename);
    } else {
        Internal::_assert(false, "[load] unsupported file extension (png|ppm supported)");
        return ImageType();
    }
}

template<typename ImageType>
void save(ImageType im, std::string filename) {
    if (Internal::ends_with_ignore_case(filename, ".png")) {
        save_png<ImageType>(im, filename);
    } else if (Internal::ends_with_ignore_case(filename, ".ppm")) {
        save_ppm<ImageType>(im, filename);
    } else {
        Internal::_assert(false, "[save] unsupported file extension (png|ppm supported)");
    }
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_IMAGE_IO_H
