// This simple PNG IO library works the Halide::Buffer<T> type or any
// other image type with the same API.

#ifndef HALIDE_IMAGE_IO_H
#define HALIDE_IMAGE_IO_H

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifndef HALIDE_NO_PNG
#include "png.h"
#endif

#ifndef HALIDE_NO_JPEG
#include "jpeglib.h"
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

template<typename To, typename From>
To convert(const From &from);

// Convert to bool
template<> inline bool convert(const bool &in) { return in; }
template<> inline bool convert(const uint8_t &in) { return in != 0; }
template<> inline bool convert(const uint16_t &in) { return in != 0; }
template<> inline bool convert(const uint32_t &in) { return in != 0; }
template<> inline bool convert(const uint64_t &in) { return in != 0; }
template<> inline bool convert(const int8_t &in) { return in != 0; }
template<> inline bool convert(const int16_t &in) { return in != 0; }
template<> inline bool convert(const int32_t &in) { return in != 0; }
template<> inline bool convert(const int64_t &in) { return in != 0; }
template<> inline bool convert(const float &in) { return in != 0; }
template<> inline bool convert(const double &in) { return in != 0; }

// Convert to u8
template<> inline uint8_t convert(const bool &in) { return in; }
template<> inline uint8_t convert(const uint8_t &in) { return in; }
template<> inline uint8_t convert(const uint16_t &in) {
    uint32_t tmp = (uint32_t)(in) + 0x80;
    // Fast approximation of div-by-257: see http://research.swtch.com/divmult
    return ((tmp * 255 + 255) >> 16);
}
template<> inline uint8_t convert(const uint32_t &in) { return (((uint64_t) in) + 0x00808080) / 0x01010101; }
// uint64 -> 8 just discards the lower 32 bits: if you were expecting more precision, well, sorry
template<> inline uint8_t convert(const uint64_t &in) { return convert<uint8_t, uint32_t>(uint32_t(in >> 32)); }
template<> inline uint8_t convert(const int8_t &in) { return convert<uint8_t, uint8_t>(in); }
template<> inline uint8_t convert(const int16_t &in) { return convert<uint8_t, uint16_t>(in); }
template<> inline uint8_t convert(const int32_t &in) { return convert<uint8_t, uint32_t>(in); }
template<> inline uint8_t convert(const int64_t &in) { return convert<uint8_t, uint64_t>(in); }
template<> inline uint8_t convert(const float &in) { return (uint8_t)(in*255.0f); }
template<> inline uint8_t convert(const double &in) { return (uint8_t)(in*255.0); }

// Convert to u16
template<> inline uint16_t convert(const bool &in) { return in; }
template<> inline uint16_t convert(const uint8_t &in) { return uint16_t(in) * 0x0101; }
template<> inline uint16_t convert(const uint16_t &in) { return in; }
template<> inline uint16_t convert(const uint32_t &in) { return in >> 16; }
template<> inline uint16_t convert(const uint64_t &in) { return in >> 48; }
template<> inline uint16_t convert(const int8_t &in) { return convert<uint16_t, uint8_t>(in); }
template<> inline uint16_t convert(const int16_t &in) { return convert<uint16_t, uint16_t>(in); }
template<> inline uint16_t convert(const int32_t &in) { return convert<uint16_t, uint32_t>(in); }
template<> inline uint16_t convert(const int64_t &in) { return convert<uint16_t, uint64_t>(in); }
template<> inline uint16_t convert(const float &in) { return (uint16_t)(in*65535.0f); }
template<> inline uint16_t convert(const double &in) { return (uint16_t)(in*65535.0); }

// Convert to u32
template<> inline uint32_t convert(const bool &in) { return in; }
template<> inline uint32_t convert(const uint8_t &in) { return uint32_t(in) * 0x01010101; }
template<> inline uint32_t convert(const uint16_t &in) { return uint32_t(in) * 0x00010001; }
template<> inline uint32_t convert(const uint32_t &in) { return in; }
template<> inline uint32_t convert(const uint64_t &in) { return (uint32_t) (in >> 32); }
template<> inline uint32_t convert(const int8_t &in) { return convert<uint32_t, uint8_t>(in); }
template<> inline uint32_t convert(const int16_t &in) { return convert<uint32_t, uint16_t>(in); }
template<> inline uint32_t convert(const int32_t &in) { return convert<uint32_t, uint32_t>(in); }
template<> inline uint32_t convert(const int64_t &in) { return convert<uint32_t, uint64_t>(in); }
template<> inline uint32_t convert(const float &in) { return (uint32_t)(in*4294967295.0); }
template<> inline uint32_t convert(const double &in) { return (uint32_t)(in*4294967295.0); }

// Convert to u64
template<> inline uint64_t convert(const bool &in) { return in; }
template<> inline uint64_t convert(const uint8_t &in) { return uint64_t(in) * 0x0101010101010101LL; }
template<> inline uint64_t convert(const uint16_t &in) { return uint64_t(in) * 0x0001000100010001LL; }
template<> inline uint64_t convert(const uint32_t &in) { return uint64_t(in) * 0x0000000100000001LL; }
template<> inline uint64_t convert(const uint64_t &in) { return in; }
template<> inline uint64_t convert(const int8_t &in) { return convert<uint64_t, uint8_t>(in); }
template<> inline uint64_t convert(const int16_t &in) { return convert<uint64_t, uint16_t>(in); }
template<> inline uint64_t convert(const int32_t &in) { return convert<uint64_t, uint64_t>(in); }
template<> inline uint64_t convert(const int64_t &in) { return convert<uint64_t, uint64_t>(in); }
template<> inline uint64_t convert(const float &in) { return convert<uint64_t, uint32_t>(in*4294967295.0); }
template<> inline uint64_t convert(const double &in) { return convert<uint64_t, uint32_t>(in*4294967295.0); }

// Convert to i8
template<> inline int8_t convert(const bool &in) { return in; }
template<> inline int8_t convert(const uint8_t &in) { return convert<uint8_t, uint8_t>(in); }
template<> inline int8_t convert(const uint16_t &in) { return convert<uint8_t, uint16_t>(in); }
template<> inline int8_t convert(const uint32_t &in) { return convert<uint8_t, uint32_t>(in); }
template<> inline int8_t convert(const uint64_t &in) { return convert<uint8_t, uint64_t>(in); }
template<> inline int8_t convert(const int8_t &in) { return convert<uint8_t, int8_t>(in); }
template<> inline int8_t convert(const int16_t &in) { return convert<uint8_t, int16_t>(in); }
template<> inline int8_t convert(const int32_t &in) { return convert<uint8_t, int32_t>(in); }
template<> inline int8_t convert(const int64_t &in) { return convert<uint8_t, int64_t>(in); }
template<> inline int8_t convert(const float &in) { return convert<uint8_t, float>(in); }
template<> inline int8_t convert(const double &in) { return convert<uint8_t, double>(in); }

// Convert to i16
template<> inline int16_t convert(const bool &in) { return in; }
template<> inline int16_t convert(const uint8_t &in) { return convert<uint16_t, uint8_t>(in); }
template<> inline int16_t convert(const uint16_t &in) { return convert<uint16_t, uint16_t>(in); }
template<> inline int16_t convert(const uint32_t &in) { return convert<uint16_t, uint32_t>(in); }
template<> inline int16_t convert(const uint64_t &in) { return convert<uint16_t, uint64_t>(in); }
template<> inline int16_t convert(const int8_t &in) { return convert<uint16_t, int8_t>(in); }
template<> inline int16_t convert(const int16_t &in) { return convert<uint16_t, int16_t>(in); }
template<> inline int16_t convert(const int32_t &in) { return convert<uint16_t, int32_t>(in); }
template<> inline int16_t convert(const int64_t &in) { return convert<uint16_t, int64_t>(in); }
template<> inline int16_t convert(const float &in) { return convert<uint16_t, float>(in); }
template<> inline int16_t convert(const double &in) { return convert<uint16_t, double>(in); }

// Convert to i32
template<> inline int32_t convert(const bool &in) { return in; }
template<> inline int32_t convert(const uint8_t &in) { return convert<uint32_t, uint8_t>(in); }
template<> inline int32_t convert(const uint16_t &in) { return convert<uint32_t, uint16_t>(in); }
template<> inline int32_t convert(const uint32_t &in) { return convert<uint32_t, uint32_t>(in); }
template<> inline int32_t convert(const uint64_t &in) { return convert<uint32_t, uint64_t>(in); }
template<> inline int32_t convert(const int8_t &in) { return convert<uint32_t, int8_t>(in); }
template<> inline int32_t convert(const int16_t &in) { return convert<uint32_t, int16_t>(in); }
template<> inline int32_t convert(const int32_t &in) { return convert<uint32_t, int32_t>(in); }
template<> inline int32_t convert(const int64_t &in) { return convert<uint32_t, int64_t>(in); }
template<> inline int32_t convert(const float &in) { return convert<uint32_t, float>(in); }
template<> inline int32_t convert(const double &in) { return convert<uint32_t, double>(in); }

// Convert to i64
template<> inline int64_t convert(const bool &in) { return in; }
template<> inline int64_t convert(const uint8_t &in) { return convert<uint64_t, uint8_t>(in); }
template<> inline int64_t convert(const uint16_t &in) { return convert<uint64_t, uint16_t>(in); }
template<> inline int64_t convert(const uint32_t &in) { return convert<uint64_t, uint32_t>(in); }
template<> inline int64_t convert(const uint64_t &in) { return convert<uint64_t, uint64_t>(in); }
template<> inline int64_t convert(const int8_t &in) { return convert<uint64_t, int8_t>(in); }
template<> inline int64_t convert(const int16_t &in) { return convert<uint64_t, int16_t>(in); }
template<> inline int64_t convert(const int32_t &in) { return convert<uint64_t, int32_t>(in); }
template<> inline int64_t convert(const int64_t &in) { return convert<uint64_t, int64_t>(in); }
template<> inline int64_t convert(const float &in) { return convert<uint64_t, float>(in); }
template<> inline int64_t convert(const double &in) { return convert<uint64_t, double>(in); }

// Convert to f32
template<> inline float convert(const bool &in) { return in; }
template<> inline float convert(const uint8_t &in) { return in/255.0f; }
template<> inline float convert(const uint16_t &in) { return in/65535.0f; }
template<> inline float convert(const uint32_t &in) { return (float) (in/4294967295.0); }
template<> inline float convert(const uint64_t &in) { return convert<float, uint32_t>(uint32_t(in >> 32)); }
template<> inline float convert(const int8_t &in) { return convert<float, uint8_t>(in); }
template<> inline float convert(const int16_t &in) { return convert<float, uint16_t>(in); }
template<> inline float convert(const int32_t &in) { return convert<float, uint64_t>(in); }
template<> inline float convert(const int64_t &in) { return convert<float, uint64_t>(in); }
template<> inline float convert(const float &in) { return in; }
template<> inline float convert(const double &in) { return (double) in; }

// Convert to f64
template<> inline double convert(const bool &in) { return in; }
template<> inline double convert(const uint8_t &in) { return in/255.0f; }
template<> inline double convert(const uint16_t &in) { return in/65535.0f; }
template<> inline double convert(const uint32_t &in) { return (double) (in/4294967295.0); }
template<> inline double convert(const uint64_t &in) { return convert<double, uint32_t>(uint32_t(in >> 32)); }
template<> inline double convert(const int8_t &in) { return convert<double, uint8_t>(in); }
template<> inline double convert(const int16_t &in) { return convert<double, uint16_t>(in); }
template<> inline double convert(const int32_t &in) { return convert<double, uint64_t>(in); }
template<> inline double convert(const int64_t &in) { return convert<double, uint64_t>(in); }
template<> inline double convert(const float &in) { return (double) in; }
template<> inline double convert(const double &in) { return in; }

inline std::string get_lowercase_extension(const std::string &path) {
    size_t last_dot = path.rfind('.');
    if (last_dot == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(last_dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

inline bool is_little_endian() {
    int value = 1;
    return ((char *) &value)[0] == 1;
}

inline uint16_t swap_endian_16(bool little_endian, uint16_t value) {
    return (little_endian) ? ((value & 0xff)<<8)|((value & 0xff00)>>8) : value;
}

struct FileOpener {
    FileOpener(const std::string &filename, const char* mode) : f(fopen(filename.c_str(), mode)) {
        // nothing
    }

    ~FileOpener() {
        if (f != nullptr) {
            fclose(f);
        }
    }

    // read a line of data, skipping lines that begin with '#"
    char *read_line(char *buf, int maxlen) {
        char *status;
        do {
            status = fgets(buf, maxlen, f);
        } while(status && buf[0] == '#');
        return(status);
    }

    // call read_line and to a sscanf() on it
    int scan_line(const char *fmt, ...) {
        char buf[1024];
        if (!read_line(buf, 1024)) {
            return 0;
        }
        va_list args;
        va_start(args, fmt);
        int result = vsscanf(buf, fmt, args);
        va_end(args);
        return result;
    }

    bool read_bytes(uint8_t *data, size_t count) {
        return fread(data, 1, count, f) == count;
    }

    template<typename T>
    bool read_vector(std::vector<T> *v) {
        return read_bytes((uint8_t *)v->data(), v->size() * sizeof(T));
    }

    bool write_bytes(const uint8_t *data, size_t count) {
        return fwrite(data, 1, count, f) == count;
    }

    template<typename T>
    bool write_vector(const std::vector<T> &v) {
        return write_bytes((const uint8_t *)v.data(), v.size() * sizeof(T));
    }


    FILE * const f;
};

#ifndef HALIDE_NO_PNG
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
#endif // HALIDE_NO_PNG

}  // namespace Internal


template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_png(const std::string &filename, ImageType *im) {
#ifdef HALIDE_NO_PNG
    return check(false, "png not supported in this build");
#else // HALIDE_NO_PNG
    using ElemType = typename ImageType::ElemType;
    png_byte header[8];
    png_structp png_ptr;
    png_infop info_ptr;

    /* open file and test for it being a png */
    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) {
        return false;
    }
    if (!check(f.read_bytes(header, sizeof(header)), "File ended before end of header")) {
        return false;
    }
    if (!check(!png_sig_cmp(header, 0, 8), "File %s is not recognized as a PNG file\n", filename.c_str())) {
        return false;
    }

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!check(png_ptr != nullptr, "png_create_read_struct failed")) {
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "png_create_info_struct failed")) {
        return false;
    }

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error during init_io")) {
        return false;
    }

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
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error during read_image")) {
        return false;
    }

    Internal::PngRowPointers row_pointers(im->height(), png_get_rowbytes(png_ptr, info_ptr));
    png_read_image(png_ptr, row_pointers.p);

    if (!check((bit_depth == 8) || (bit_depth == 16), "Can only handle 8-bit or 16-bit pngs")) {
        return false;
    }

    // convert the data to ImageType::ElemType

    int64_t c_stride = (im->channels() == 1) ? 0 : ((&(*im)(0, 0, 1)) - (&(*im)(0, 0, 0)));
    ElemType *ptr = (ElemType*)im->data();
    if (bit_depth == 8) {
        for (int y = 0; y < im->height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers.p[y]);
            for (int x = 0; x < im->width(); x++) {
                for (int c = 0; c < im->channels(); c++) {
                    ptr[c*c_stride] = Internal::convert<ElemType>(*srcPtr++);
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
                    ptr[c*c_stride] = Internal::convert<ElemType>(lo);
                }
                ptr++;
            }
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    im->set_host_dirty();
    return true;
#endif // HALIDE_NO_PNG
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_png(ImageType &im, const std::string &filename) {
#ifdef HALIDE_NO_PNG
    return check(false, "png not supported in this build");
#else // HALIDE_NO_PNG
    using ElemType = typename ImageType::ElemType;
    png_structp png_ptr;
    png_infop info_ptr;
    png_byte color_type;

    im.copy_to_host();

    if (!check(im.channels() > 0 && im.channels() < 5,
           "Can't write PNG files that have other than 1, 2, 3, or 4 channels")) {
        return false;
    }

    png_byte color_types[4] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
                               PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA
                              };
    color_type = color_types[im.channels() - 1];

    // open file
    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "[write_png_file] File %s could not be opened for writing\n", filename.c_str())) {
        return false;
    }

    // initialize stuff
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!check(png_ptr != nullptr, "[write_png_file] png_create_write_struct failed")) {
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "[write_png_file] png_create_info_struct failed")) {
        return false;
    }

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during init_io")) {
        return false;
    }

    png_init_io(png_ptr, f.f);

    constexpr unsigned int bit_depth = (sizeof(ElemType) == 1) ? 8 : 16;

    // write header
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing header")) {
        return false;
    }

    png_set_IHDR(png_ptr, info_ptr, im.width(), im.height(),
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    Internal::PngRowPointers row_pointers(im.height(), png_get_rowbytes(png_ptr, info_ptr));

    // We don't require that the image type provided has any
    // particular way to get at the strides, so take differences of
    // addresses of pixels to compute them.
    int64_t c_stride = (im.channels() == 1) ? 0 : ((&im(0, 0, 1)) - (&im(0, 0, 0)));
    int64_t x_stride = (int)((&im(1, 0, 0)) - (&im(0, 0, 0)));
    ElemType *srcPtr = (ElemType*)im.data();

    for (int y = 0; y < im.height(); y++) {
        uint8_t *dstPtr = (uint8_t *)(row_pointers.p[y]);
        if (bit_depth == 16) {
            // convert to uint16_t
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint16_t out = Internal::convert<uint16_t>(srcPtr[c*c_stride]);
                    *dstPtr++ = out >> 8;
                    *dstPtr++ = out & 0xff;
                }
                srcPtr += x_stride;
            }
        } else if (bit_depth == 8) {
            // convert to uint8_t
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint8_t out = Internal::convert<uint8_t>(srcPtr[c*c_stride]);
                    *dstPtr++ = out;
                }
                srcPtr += x_stride;
            }
        }
    }

    // write data
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing bytes")) {
        return false;
    }

    png_write_image(png_ptr, row_pointers.p);

    // finish write
    if (!check(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during end of write")) {
        return false;
    }

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    return true;
#endif // HALIDE_NO_PNG
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_pgm(const std::string &filename, ImageType *im) {
    using ElemType = typename ImageType::ElemType;

    /* open file and test for it being a pgm */
    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) {
        return false;
    }

    int width, height, maxval;
    char header[256];
    bool fmt_binary = false;

    if (!check(f.scan_line("%255s", header) == 1, "Could not read PGM header")) {
        return false;
    }
    if (header == std::string("P5") || header == std::string("p5")) {
        fmt_binary = true;
    }
    if (!check(fmt_binary, "Input is not binary PGM")) {
        return false;
    }

    if (!check(f.scan_line("%d %d\n", &width, &height) == 2, "Could not read PGM width and height")) {
        return false;
    }
    if (!check(f.scan_line("%d", &maxval) == 1, "Could not read PGM max value")) {
        return false;
    }

    // Graymap
    *im = ImageType(width, height);

    // convert the data to ImageType::ElemType
    if (maxval == 255) {
        std::vector<uint8_t> data(width*height);
        if (!check(f.read_vector(&data), "Could not read PGM 8-bit data")) {
            return false;
        }
        ElemType *im_data = (ElemType*) im->data();
        uint8_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                *im_data++ = Internal::convert<ElemType>(*p++);
            }
        }
    } else if (maxval == 65535) {
        bool little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height);
        if (!check(f.read_vector(&data), "Could not read PGM 16-bit data")) {
            return false;
        }
        ElemType *im_data = (ElemType*) im->data();
        uint16_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value = Internal::swap_endian_16(little_endian, *p++);
                *im_data++ = Internal::convert<ElemType>(value);
            }
        }
    } else {
        return check(false, "Invalid bit depth in PGM\n");
    }
    (*im)(0,0,0) = (*im)(0,0,0);      /* Mark dirty inside read/write functions. */

    return true;
}

// "im" is not const-ref because copy_to_host() is not const.
// Optional channel parameter for specifying which color to save as a graymap
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_pgm_channel(ImageType &im, const std::string &filename, unsigned int channel) {
    using ElemType = typename ImageType::ElemType;

    im.copy_to_host();

    constexpr unsigned int bit_depth = sizeof(ElemType) == 1 ? 8: 16;
    unsigned int num_channels = im.channels();

    if (!check(channel >= 0, "Selected channel %d not available in image\n", channel)) {
        return false;
    }
    if (!check(channel < num_channels, "Selected channel %d not available in image\n", channel)) {
        return false;
    }
    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File %s could not be opened for writing\n", filename.c_str())) {
        return false;
    }
    fprintf(f.f, "P5\n%d %d\n%d\n", im.width(), im.height(), (1<<bit_depth)-1);
    int width = im.width(), height = im.height();

    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height);
        uint8_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                *p++ = Internal::convert<uint8_t>(im(x, y, channel));
            }
        }
        if (!check(f.write_vector(data), "Could not write PGM 8-bit data")) {
            return false;
        }
    } else if (bit_depth == 16) {
        bool little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height);
        uint16_t *p = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value = Internal::convert<uint16_t>(im(x, y, channel));
                *p++ = Internal::swap_endian_16(little_endian, value);
            }
        }
        if (!check(f.write_vector(data), "Could not write PGM 16-bit data")) {
            return false;
        }
    } else {
        return check(false, "We only support saving 8- and 16-bit images.");
    }
    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_pgm(ImageType &im, const std::string &filename) {
    return save_pgm_channel<ImageType, check>(im, filename, /* channel */ 0);
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_ppm(const std::string &filename, ImageType *im) {
    using ElemType = typename ImageType::ElemType;

    /* open file and test for it being a ppm */
    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File %s could not be opened for reading\n", filename.c_str())) {
        return false;
    }

    int width, height, maxval;
    char header[256];
    bool fmt_binary = false;

    if (!check(f.scan_line("%255s", header) == 1, "Could not read PPM header")) {
        return false;
    }
    if (header == std::string("P6") || header == std::string("p6")) {
        fmt_binary = true;
    }
    if (!check(fmt_binary, "Input is not binary PPM")) {
        return false;
    }

    if (!check(f.scan_line("%d %d\n", &width, &height) == 2, "Could not read PPM width and height")) {
        return false;
    }
    if (!check(f.scan_line("%d", &maxval) == 1, "Could not read PPM max value")) {
        return false;
    }

    constexpr int channels = 3;
    *im = ImageType(width, height, channels);

    // convert the data to ImageType::ElemType
    if (maxval == 255) {
        std::vector<uint8_t> data(width*height*3);
        if (!check(f.read_vector(&data), "Could not read PPM 8-bit data")) {
            return false;
        }
        ElemType *im_data = (ElemType*) im->data();
        uint8_t *row = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                im_data[(0*height+y)*width+x] = Internal::convert<ElemType>(*row++);
                im_data[(1*height+y)*width+x] = Internal::convert<ElemType>(*row++);
                im_data[(2*height+y)*width+x] = Internal::convert<ElemType>(*row++);
            }
        }
    } else if (maxval == 65535) {
        bool little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height*3);
        if (!check(f.read_vector(&data), "Could not read PPM 16-bit data")) {
            return false;
        }
        ElemType *im_data = (ElemType*) im->data();
        uint16_t *row = &data[0];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t value;
                value = Internal::swap_endian_16(little_endian, *row++);
                im_data[(0*height+y)*width+x] = Internal::convert<ElemType>(value);
                value = Internal::swap_endian_16(little_endian, *row++);
                im_data[(1*height+y)*width+x] = Internal::convert<ElemType>(value);
                value = Internal::swap_endian_16(little_endian, *row++);
                im_data[(2*height+y)*width+x] = Internal::convert<ElemType>(value);
            }
        }
    } else {
        return check(false, "Invalid bit depth in PPM.");
    }
    (*im)(0,0,0) = (*im)(0,0,0);      /* Mark dirty inside read/write functions. */
    return true;
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_ppm(ImageType &im, const std::string &filename) {
    using ElemType = typename ImageType::ElemType;

    if (!check(im.channels() == 3, "save_ppm() requires a 3-channel image.\n")) { 
        return false; 
    }

    im.copy_to_host();

    constexpr unsigned int bit_depth = sizeof(ElemType) == 1 ? 8: 16;

    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File %s could not be opened for writing\n", filename.c_str())) {
        return false;
    }
    fprintf(f.f, "P6\n%d %d\n%d\n", im.width(), im.height(), (1<<bit_depth)-1);
    int width = im.width();
    int height = im.height();
    int channels = im.channels();

    if (bit_depth == 8) {
        std::vector<uint8_t> data(width*height*3);
        uint8_t *p = &data[0];
        // unroll inner loop for 3 channel RGB (common case)
        if (channels == 3) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    *p++ = Internal::convert<uint8_t>(im(x, y, 0));
                    *p++ = Internal::convert<uint8_t>(im(x, y, 1));
                    *p++ = Internal::convert<uint8_t>(im(x, y, 2));
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    for (int c = 0; c < channels; c++) {
                        *p++ = Internal::convert<uint8_t>(im(x, y, c));
                    }
                }
            }
        }
        if (!check(f.write_vector(data), "Could not write PPM 8-bit data")) {
            return false;
        }
    } else if (bit_depth == 16) {
        bool little_endian = Internal::is_little_endian();
        std::vector<uint16_t> data(width*height*3);
        uint16_t *p = &data[0];
        // unroll inner loop for 3 channel RGB (common case)
        if (channels == 3) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint16_t value0 = Internal::convert<uint16_t>(im(x, y, 0));
                    *p++ = Internal::swap_endian_16(little_endian, value0);
                    uint16_t value1 = Internal::convert<uint16_t>(im(x, y, 1));
                    *p++ = Internal::swap_endian_16(little_endian, value1);
                    uint16_t value2 = Internal::convert<uint16_t>(im(x, y, 2));
                    *p++ = Internal::swap_endian_16(little_endian, value2);
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    for (int c = 0; c < channels; c++) {
                        uint16_t value = Internal::convert<uint16_t>(im(x, y, c));
                        *p++ = Internal::swap_endian_16(little_endian, value);
                    }
                }
            }
        }
        if (!check(f.write_vector(data), "Could not write PPM 16-bit data")) {
            return false;
        }
    } else {
        return check(false, "We only support saving 8- and 16-bit images.");
    }
    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_jpg(ImageType &im, const std::string &filename) {
#ifdef HALIDE_NO_JPEG
    check(false, "jpg not supported in this build\n");
    return false;
#else
    im.copy_to_host();

    int channels = 1;
    if (im.dimensions() == 3) {
        channels = im.channels();
    }

    if (!check((im.dimensions() == 2 || im.dimensions() == 3) &&
               (channels == 1 || channels == 3),
               "Can only save jpg images with 1 or 3 channels\n")) {
        return false;
    }

    // TODO: Make this an argument?
    constexpr int quality = 99;

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr,
               "File %s could not be opened for writing\n", filename.c_str())) {
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f.f);

    cinfo.image_width = im.width();
    cinfo.image_height = im.height();
    cinfo.input_components = channels;
    if (channels == 3) {
        cinfo.in_color_space = JCS_RGB;
    } else { // channels must be 1
        cinfo.in_color_space = JCS_GRAYSCALE;
    }

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    std::vector<JSAMPLE> row(im.width() * channels);
    for (int y = 0; y < im.height(); y++) {
        JSAMPLE *dst = row.data();
        if (im.dimensions() == 2) {
            for (int x = 0; x < im.width(); x++) {
                *dst++ = Internal::convert<JSAMPLE>(im(x, y));
            }
        } else {
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < channels; c++) {
                    *dst++ = Internal::convert<JSAMPLE>(im(x, y, c));
                }
            }
        }
        JSAMPROW row_ptr = row.data();
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return true;
#endif
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_jpg(const std::string &filename, ImageType *im) {
#ifdef HALIDE_NO_JPEG
    check(false, "jpg not supported in this build\n");
    return false;
#else
    using ElemType = typename ImageType::ElemType;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr,
               "File %s could not be opened for reading\n", filename.c_str())) {
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f.f);

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int channels = cinfo.output_components;
    if (channels > 1) {
        *im = ImageType(cinfo.output_width, cinfo.output_height, channels);
    } else {
        *im = ImageType(cinfo.output_width, cinfo.output_height);
    }
    std::vector<JSAMPLE> row(im->width() * channels);
    for (int y = 0; y < im->height(); y++) {
        JSAMPLE *src = row.data();
        jpeg_read_scanlines(&cinfo, &src, 1);
        if (channels > 1) {
            for (int x = 0; x < im->width(); x++) {
                for (int c = 0; c < channels; c++) {
                    (*im)(x, y, c) = Internal::convert<ElemType>(*src++);
                }
            }
        } else {
            for (int x = 0; x < im->width(); x++) {
                (*im)(x, y) = Internal::convert<ElemType>(*src++);
            }
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return true;
#endif
}

namespace Internal {

template<typename ImageType, Internal::CheckFunc check>
struct ImageIO {
    std::function<bool(const std::string &, ImageType *)> load;
    std::function<bool(ImageType &im, const std::string &)> save;
};

template<typename ImageType, Internal::CheckFunc check>
bool find_imageio(const std::string &filename, ImageIO<ImageType, check> *result) {
    const std::map<std::string, ImageIO<ImageType, check>> m = {
        {"jpeg", {load_jpg<ImageType, check>, save_jpg<ImageType, check>}},
        {"jpg", {load_jpg<ImageType, check>, save_jpg<ImageType, check>}},
        {"pgm", {load_pgm<ImageType, check>, save_pgm<ImageType, check>}},
        {"png", {load_png<ImageType, check>, save_png<ImageType, check>}},
        {"ppm", {load_ppm<ImageType, check>, save_ppm<ImageType, check>}}
    };
    auto it = m.find(Internal::get_lowercase_extension(filename));
    if (it != m.end()) {
        *result = it->second;
        return true;
    }

    std::string err = "unsupported file extension, supported are:";
    for (auto &it : m) {
        err += " " + it.first;
    }
    err += "\n";
    return check(false, err.c_str());
}

// Given something like ImageType<Foo>, produce typedef ImageType<Bar>
template<typename ImageType, typename ElemType>
struct ImageTypeWithElemType {
    using type = decltype(std::declval<ImageType>().template as<ElemType>());
};

// Must be constexpr to allow use in case clauses.
inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return (((int) code) << 8) | bits;
}

}  // namespace Internal

struct ImageTypeConversion {
    // Convert an Image from one ElemType to another, where the src and
    // dst types are statically known (e.g. Buffer<uint8_t> -> Buffer<float>).
    // Note that this does conversion with scaling -- intepreting integers
    // as fixed-point numbers between 0 and 1 -- not merely C-style casting.
    //
    // You'd normally call this with an explicit type for DstElemType and
    // allow ImageType to be inferred, e.g.
    //     Buffer<uint8_t> src = ...;
    //     Buffer<float> dst = convert_image<float>(src);
    template<typename DstElemType, typename ImageType,
             typename std::enable_if<ImageType::has_static_halide_type>::type * = nullptr>
    static auto convert_image(const ImageType &src) -> 
            typename Internal::ImageTypeWithElemType<ImageType, DstElemType>::type {
        // The enable_if ensures this will never fire; this is here primarily
        // as documentation and a backstop against breakage.
        static_assert(ImageType::has_static_halide_type, 
                      "This variant of convert_image() requires a statically-typed image");

        using SrcImageType = ImageType;
        using SrcElemType = typename SrcImageType::ElemType;

        using DstImageType = typename Internal::ImageTypeWithElemType<ImageType, DstElemType>::type;

        DstImageType dst = DstImageType::make_with_shape_of(src);
        const auto converter = [](DstElemType &dst_elem, SrcElemType src_elem) {
            dst_elem = Internal::convert<DstElemType>(src_elem);
        };
        // TODO: do we need src.copy_to_host() here?
        dst.for_each_value(converter, src);

        return dst;
    }

    // Convert an Image from one ElemType to another, where the dst type is statically
    // known but the src type is not (e.g. Buffer<> -> Buffer<float>).
    // You'd normally call this with an explicit type for DstElemType and
    // allow ImageType to be inferred, e.g.
    //     Buffer<uint8_t> src = ...;
    //     Buffer<float> dst = convert_image<float>(src);
    template<typename DstElemType, typename ImageType,
             typename std::enable_if<!ImageType::has_static_halide_type>::type * = nullptr>
    static auto convert_image(const ImageType &src) -> 
            typename Internal::ImageTypeWithElemType<ImageType, DstElemType>::type {
        // The enable_if ensures this will never fire; this is here primarily
        // as documentation and a backstop against breakage.
        static_assert(!ImageType::has_static_halide_type, 
                      "This variant of convert_image() requires a dynamically-typed image");

        const halide_type_t src_type = src.type();
        switch (Internal::halide_type_code((halide_type_code_t) src_type.code, src_type.bits)) {
            case Internal::halide_type_code(halide_type_float, 32):
                return convert_image<DstElemType>(src.template as<float>());
            case Internal::halide_type_code(halide_type_float, 64): 
                return convert_image<DstElemType>(src.template as<double>());
            case Internal::halide_type_code(halide_type_int, 8): 
                return convert_image<DstElemType>(src.template as<int8_t>());
            case Internal::halide_type_code(halide_type_int, 16): 
                return convert_image<DstElemType>(src.template as<int16_t>());
            case Internal::halide_type_code(halide_type_int, 32): 
                return convert_image<DstElemType>(src.template as<int32_t>());
            case Internal::halide_type_code(halide_type_int, 64): 
                return convert_image<DstElemType>(src.template as<int64_t>());
            case Internal::halide_type_code(halide_type_uint, 1): 
                return convert_image<DstElemType>(src.template as<bool>());
            case Internal::halide_type_code(halide_type_uint, 8): 
                return convert_image<DstElemType>(src.template as<uint8_t>());
            case Internal::halide_type_code(halide_type_uint, 16): 
                return convert_image<DstElemType>(src.template as<uint16_t>());
            case Internal::halide_type_code(halide_type_uint, 32): 
                return convert_image<DstElemType>(src.template as<uint32_t>());
            case Internal::halide_type_code(halide_type_uint, 64): 
                return convert_image<DstElemType>(src.template as<uint64_t>());
            default:
                assert(!"Unsupported type");
                using DstImageType = typename Internal::ImageTypeWithElemType<ImageType, DstElemType>::type;
                return DstImageType();
        }
    }

    // Convert an Image from one ElemType to another, where the src type
    // is statically known but the dst type is not 
    // (e.g. Buffer<uint8_t> -> Buffer<>(halide_type_t)). 
    template <typename DstElemType = void,
              typename ImageType, 
              typename std::enable_if<ImageType::has_static_halide_type>::type * = nullptr>
    static auto convert_image(const ImageType &src, const halide_type_t &dst_type) -> 
            typename Internal::ImageTypeWithElemType<ImageType, void>::type {
        // The enable_if ensures this will never fire; this is here primarily
        // as documentation and a backstop against breakage.
        static_assert(ImageType::has_static_halide_type, 
                      "This variant of convert_image() requires a statically-typed image");

        // Call the appropriate static-to-static conversion routine
        // based on the desired dst type.
        switch (Internal::halide_type_code((halide_type_code_t) dst_type.code, dst_type.bits)) {
            case Internal::halide_type_code(halide_type_float, 32): 
                return convert_image<float>(src);
            case Internal::halide_type_code(halide_type_float, 64): 
                return convert_image<double>(src);
            case Internal::halide_type_code(halide_type_int, 8): 
                return convert_image<int8_t>(src);
            case Internal::halide_type_code(halide_type_int, 16): 
                return convert_image<int16_t>(src);
            case Internal::halide_type_code(halide_type_int, 32): 
                return convert_image<int32_t>(src);
            case Internal::halide_type_code(halide_type_int, 64): 
                return convert_image<int64_t>(src);
            case Internal::halide_type_code(halide_type_uint, 1): 
                return convert_image<bool>(src);
            case Internal::halide_type_code(halide_type_uint, 8): 
                return convert_image<uint8_t>(src);
            case Internal::halide_type_code(halide_type_uint, 16): 
                return convert_image<uint16_t>(src);
            case Internal::halide_type_code(halide_type_uint, 32): 
                return convert_image<uint32_t>(src);
            case Internal::halide_type_code(halide_type_uint, 64): 
                return convert_image<uint64_t>(src);
            default:
                assert(!"Unsupported type");
                return ImageType();
        }
    }

    // Convert an Image from one ElemType to another, where neither src type
    // nor dst type are statically known
    // (e.g. Buffer<>(halide_type_t) -> Buffer<>(halide_type_t)). 
    template <typename DstElemType = void,
              typename ImageType, 
              typename std::enable_if<!ImageType::has_static_halide_type>::type * = nullptr>
    static auto convert_image(const ImageType &src, const halide_type_t &dst_type) -> 
            typename Internal::ImageTypeWithElemType<ImageType, void>::type {
        // The enable_if ensures this will never fire; this is here primarily
        // as documentation and a backstop against breakage.
        static_assert(!ImageType::has_static_halide_type, 
                      "This variant of convert_image() requires a dynamically-typed image");

        // Sniff the runtime type of src, coerce it to that type using as<>(),
        // and call the static-to-dynamic variant of this method. (Note that
        // this forces instantiation of the complete any-to-any conversion
        // matrix of code.)
        const halide_type_t src_type = src.type();
        switch (Internal::halide_type_code((halide_type_code_t) src_type.code, src_type.bits)) {
            case Internal::halide_type_code(halide_type_float, 32): 
                return convert_image(src.template as<float>(), dst_type);
            case Internal::halide_type_code(halide_type_float, 64): 
                return convert_image(src.template as<double>(), dst_type);
            case Internal::halide_type_code(halide_type_int, 8): 
                return convert_image(src.template as<int8_t>(), dst_type);
            case Internal::halide_type_code(halide_type_int, 16): 
                return convert_image(src.template as<int16_t>(), dst_type);
            case Internal::halide_type_code(halide_type_int, 32): 
                return convert_image(src.template as<int32_t>(), dst_type);
            case Internal::halide_type_code(halide_type_int, 64): 
                return convert_image(src.template as<int64_t>(), dst_type);
            case Internal::halide_type_code(halide_type_uint, 1): 
                return convert_image(src.template as<bool>(), dst_type);
            case Internal::halide_type_code(halide_type_uint, 8): 
                return convert_image(src.template as<uint8_t>(), dst_type);
            case Internal::halide_type_code(halide_type_uint, 16): 
                return convert_image(src.template as<uint16_t>(), dst_type);
            case Internal::halide_type_code(halide_type_uint, 32): 
                return convert_image(src.template as<uint32_t>(), dst_type);
            case Internal::halide_type_code(halide_type_uint, 64): 
                return convert_image(src.template as<uint64_t>(), dst_type);
            default:
                assert(!"Unsupported type");
                return ImageType();
        }
    }
};

// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load(const std::string &filename, ImageType *im) {
    Internal::ImageIO<ImageType, check> imageio;
    if (!Internal::find_imageio<ImageType, check>(filename, &imageio)) {
        return false;
    }
    return imageio.load(filename, im);
}

// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save(ImageType &im, const std::string &filename) {
    Internal::ImageIO<ImageType, check> imageio;
    if (!Internal::find_imageio<ImageType, check>(filename, &imageio)) {
        return false;
    }
    return imageio.save(im, filename);
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
