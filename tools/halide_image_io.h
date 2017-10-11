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
#include <set>
#include <string>
#include <vector>
#include <cctype>

#ifndef HALIDE_NO_PNG
#include "png.h"
#endif

#ifndef HALIDE_NO_JPEG
#include "jpeglib.h"
#endif

#include "HalideRuntime.h"  // for halide_type_t

namespace Halide {
namespace Tools {

struct FormatInfo {
    halide_type_t type;
    int dimensions;

    bool operator<(const FormatInfo &other) const {
        if (type.code < other.type.code) {
            return true;
        } else if (type.code > other.type.code) {
            return false;
        }
        if (type.bits < other.type.bits) {
            return true;
        } else if (type.bits > other.type.bits) {
            return false;
        }
        if (type.lanes < other.type.lanes) {
            return true;
        } else if (type.lanes > other.type.lanes) {
            return false;
        }
        return (dimensions < other.dimensions);
    }
};

namespace Internal {

typedef bool (*CheckFunc)(bool condition, const char* msg);

inline bool CheckFail(bool condition, const char* msg) {
    if (!condition) {
        fprintf(stderr, "%s\n", msg);
        exit(-1);
    }
    return condition;
}

inline bool CheckReturn(bool condition, const char* msg) {
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
template<> inline uint8_t convert(const uint32_t &in) { return (uint8_t) ((((uint64_t) in) + 0x00808080) / 0x01010101); }
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
template<> inline uint64_t convert(const float &in) { return convert<uint64_t, uint32_t>((uint32_t)(in*4294967295.0)); }
template<> inline uint64_t convert(const double &in) { return convert<uint64_t, uint32_t>((uint32_t)(in*4294967295.0)); }

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
template<> inline float convert(const double &in) { return (float) in; }

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

inline std::string to_lowercase(const std::string &s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

inline std::string get_lowercase_extension(const std::string &path) {
    size_t last_dot = path.rfind('.');
    if (last_dot == std::string::npos) {
        return "";
    }
    return to_lowercase(path.substr(last_dot + 1));
}

template<typename ElemType>
ElemType read_big_endian(const uint8_t *src);

template<>
inline uint8_t read_big_endian(const uint8_t *src) {
    return *src;
}

template<>
inline uint16_t read_big_endian(const uint8_t *src) {
    return (((uint16_t) src[0]) << 8) | ((uint16_t) src[1]);
}

template<typename ElemType>
void write_big_endian(const ElemType &src, uint8_t *dst);

template<>
inline void write_big_endian(const uint8_t &src, uint8_t *dst) {
    *dst = src;
}

template<>
inline void write_big_endian(const uint16_t &src, uint8_t *dst) {
    dst[0] = src >> 8;
    dst[1] = src & 0xff;
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

    bool read_bytes(void *data, size_t count) {
        return fread(data, 1, count, f) == count;
    }

    template<typename T, size_t N>
    bool read_array(T (&data)[N]) {
        return read_bytes(&data[0], sizeof(T) * N);
    }

    template<typename T>
    bool read_vector(std::vector<T> *v) {
        return read_bytes(v->data(), v->size() * sizeof(T));
    }

    bool write_bytes(const void *data, size_t count) {
        return fwrite(data, 1, count, f) == count;
    }

    template<typename T>
    bool write_vector(const std::vector<T> &v) {
        return write_bytes(v.data(), v.size() * sizeof(T));
    }

    template<typename T, size_t N>
    bool write_array(const T (&data)[N]) {
        return write_bytes(&data[0], sizeof(T) * N);
    }

    FILE * const f;
};

// Read a row of ElemTypes from a byte buffer and copy them into a specific image row.
// Multibyte elements are assumed to be big-endian.
template<typename ElemType, typename ImageType>
void read_big_endian_row(const uint8_t *src, int y, ImageType *im) {
    auto im_typed = im->template as<ElemType>();
    const int xmin = im_typed.dim(0).min();
    const int xmax = im_typed.dim(0).max();
    if (im_typed.dimensions() > 2) {
        const int cmin = im_typed.dim(2).min();
        const int cmax = im_typed.dim(2).max();
        for (int x = xmin; x <= xmax; x++) {
            for (int c = cmin; c <= cmax; c++) {
                im_typed(x, y, c+cmin) = read_big_endian<ElemType>(src);
                src += sizeof(ElemType);
            }
        }
    } else {
        for (int x = xmin; x <= xmax; x++) {
            im_typed(x, y) = read_big_endian<ElemType>(src);
            src += sizeof(ElemType);
        }
    }
}

// Copy a row from an image into a byte buffer.
// Multibyte elements are written in big-endian layout.
template<typename ElemType, typename ImageType>
void write_big_endian_row(const ImageType &im, int y, uint8_t *dst) {
    auto im_typed = im.template as<ElemType>();
    const int xmin = im_typed.dim(0).min();
    const int xmax = im_typed.dim(0).max();
    if (im_typed.dimensions() > 2) {
        const int cmin = im_typed.dim(2).min();
        const int cmax = im_typed.dim(2).max();
        for (int x = xmin; x <= xmax; x++) {
            for (int c = cmin; c <= cmax; c++) {
                write_big_endian<ElemType>(im_typed(x, y, c), dst);
                dst += sizeof(ElemType);
            }
        }
    } else {
        for (int x = xmin; x <= xmax; x++) {
            write_big_endian<ElemType>(im_typed(x, y), dst);
            dst += sizeof(ElemType);
        }
    }
}

#ifndef HALIDE_NO_PNG

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_png(const std::string &filename, ImageType *im) {
    static_assert(!ImageType::has_static_halide_type, "");

    /* open file and test for it being a png */
    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File could not be opened for reading")) {
        return false;
    }
    png_byte header[8];
    if (!check(f.read_array(header), "File ended before end of header")) {
        return false;
    }
    if (!check(!png_sig_cmp(header, 0, 8), "File is not recognized as a PNG file")) {
        return false;
    }

    /* initialize stuff */
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!check(png_ptr != nullptr, "png_create_read_struct failed")) {
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "png_create_info_struct failed")) {
        return false;
    }

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error loading PNG")) {
        return false;
    }

    png_init_io(png_ptr, f.f);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    const int width = png_get_image_width(png_ptr, info_ptr);
    const int height = png_get_image_height(png_ptr, info_ptr);
    const int channels = png_get_channels(png_ptr, info_ptr);
    const int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    const halide_type_t im_type(halide_type_uint, bit_depth);
    std::vector<int> im_dimensions = { width, height };
    if (channels != 1) {
        im_dimensions.push_back(channels);
    }

    *im = ImageType(im_type, im_dimensions);

    png_read_update_info(png_ptr, info_ptr);

    auto copy_to_image = bit_depth == 8 ?
        Internal::read_big_endian_row<uint8_t, ImageType> :
        Internal::read_big_endian_row<uint16_t, ImageType>;

    std::vector<uint8_t> row(png_get_rowbytes(png_ptr, info_ptr));
    const int ymin = im->dim(1).min();
    const int ymax = im->dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        png_read_row(png_ptr, row.data(), nullptr);
        copy_to_image(row.data(), y, im);
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return true;
}

inline const std::set<FormatInfo> &query_png() {
    static std::set<FormatInfo> info = {
        { halide_type_t(halide_type_uint, 8), 2 },
        { halide_type_t(halide_type_uint, 16), 2 },
        { halide_type_t(halide_type_uint, 8), 3 },
        { halide_type_t(halide_type_uint, 16), 3 }
    };
    return info;
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_png(ImageType &im, const std::string &filename) {
    static_assert(!ImageType::has_static_halide_type, "");

    im.copy_to_host();

    const int width = im.width();
    const int height = im.height();
    const int channels = im.channels();

    if (!check(channels >= 1 && channels <= 4,
           "Can't write PNG files that have other than 1, 2, 3, or 4 channels")) {
        return false;
    }

    const png_byte color_types[4] = {
        PNG_COLOR_TYPE_GRAY,
        PNG_COLOR_TYPE_GRAY_ALPHA,
        PNG_COLOR_TYPE_RGB,
        PNG_COLOR_TYPE_RGB_ALPHA
    };
    png_byte color_type = color_types[channels - 1];

    // open file
    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "[write_png_file] File could not be opened for writing")) {
        return false;
    }

    // initialize stuff
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!check(png_ptr != nullptr, "[write_png_file] png_create_write_struct failed")) {
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!check(info_ptr != nullptr, "[write_png_file] png_create_info_struct failed")) {
        return false;
    }

    if (!check(!setjmp(png_jmpbuf(png_ptr)), "Error saving PNG")) {
        return false;
    }

    png_init_io(png_ptr, f.f);

    const halide_type_t im_type = im.type();
    const int bit_depth = im_type.bits;

    png_set_IHDR(png_ptr, info_ptr, width, height,
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    auto copy_from_image = bit_depth == 8 ?
        Internal::write_big_endian_row<uint8_t, ImageType> :
        Internal::write_big_endian_row<uint16_t, ImageType>;

    std::vector<uint8_t> row(png_get_rowbytes(png_ptr, info_ptr));
    const int ymin = im.dim(1).min();
    const int ymax = im.dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        copy_from_image(im, y, row.data());
        png_write_row(png_ptr, row.data());
    }
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return true;
}

#endif // not HALIDE_NO_PNG

template<Internal::CheckFunc check>
bool read_pnm_header(Internal::FileOpener &f, const std::string &hdr_fmt, int *width, int *height, int *bit_depth) {
    if (!check(f.f != nullptr, "File could not be opened for reading")) {
        return false;
    }

    char header[256];
    if (!check(f.scan_line("%255s", header) == 1, "Could not read header")) {
        return false;
    }

    if (!check(to_lowercase(hdr_fmt) == to_lowercase(header), "Unexpected file header")) {
        return false;
    }

    if (!check(f.scan_line("%d %d\n", width, height) == 2, "Could not read width and height")) {
        return false;
    }

    int maxval;
    if (!check(f.scan_line("%d", &maxval) == 1, "Could not read max value")) {
        return false;
    }
    if (maxval == 255) {
        *bit_depth = 8;
    } else if (maxval == 65535) {
        *bit_depth = 16;
    } else {
        *bit_depth = 0;
        return check(false, "Invalid bit depth");
    }

    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_pnm(const std::string &filename, int channels, ImageType *im) {
    static_assert(!ImageType::has_static_halide_type, "");

    const char *hdr_fmt = channels == 3 ? "P6" : "P5";

    Internal::FileOpener f(filename, "rb");
    int width, height, bit_depth;
    if (!Internal::read_pnm_header<check>(f, hdr_fmt, &width, &height, &bit_depth)) {
        return false;
    }

    const halide_type_t im_type(halide_type_uint, bit_depth);
    std::vector<int> im_dimensions = { width, height };
    if (channels > 1) {
        im_dimensions.push_back(channels);
    }
    *im = ImageType(im_type, im_dimensions);

    auto copy_to_image = bit_depth == 8 ?
        Internal::read_big_endian_row<uint8_t, ImageType> :
        Internal::read_big_endian_row<uint16_t, ImageType>;

    std::vector<uint8_t> row(width * channels * (bit_depth / 8));
    const int ymin = im->dim(1).min();
    const int ymax = im->dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        if (!check(f.read_vector(&row), "Could not read data")) {
            return false;
        }
        copy_to_image(row.data(), y, im);
    }

    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_pnm(ImageType &im, const int channels, const std::string &filename) {
    static_assert(!ImageType::has_static_halide_type, "");

    if (!check(im.channels() == channels, "Wrong number of channels")) {
        return false;
    }

    im.copy_to_host();

    const halide_type_t im_type = im.type();
    const int width = im.width();
    const int height = im.height();
    const int bit_depth = im_type.bits;

    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File could not be opened for writing")) {
        return false;
    }
    const char *hdr_fmt = channels == 3 ? "P6" : "P5";
    fprintf(f.f, "%s\n%d %d\n%d\n", hdr_fmt, width, height, (1<<bit_depth)-1);

    auto copy_from_image = bit_depth == 8 ?
        Internal::write_big_endian_row<uint8_t, ImageType> :
        Internal::write_big_endian_row<uint16_t, ImageType>;

    std::vector<uint8_t> row(width * channels * (bit_depth / 8));
    const int ymin = im.dim(1).min();
    const int ymax = im.dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        copy_from_image(im, y, row.data());
        if (!check(f.write_vector(row), "Could not write data")) {
            return false;
        }
    }

    return true;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_pgm(const std::string &filename, ImageType *im) {
    return Internal::load_pnm<ImageType, check>(filename, 1, im);
}

inline const std::set<FormatInfo> &query_pgm() {
    static std::set<FormatInfo> info = {
        { halide_type_t(halide_type_uint, 8), 2 },
        { halide_type_t(halide_type_uint, 16), 2 }
    };
    return info;
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_pgm(ImageType &im, const std::string &filename) {
    return Internal::save_pnm<ImageType, check>(im, 1, filename);
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_ppm(const std::string &filename, ImageType *im) {
    return Internal::load_pnm<ImageType, check>(filename, 3, im);
}

inline const std::set<FormatInfo> &query_ppm() {
    static std::set<FormatInfo> info = {
        { halide_type_t(halide_type_uint, 8), 3 },
        { halide_type_t(halide_type_uint, 16), 3 }
    };
    return info;
}

// "im" is not const-ref because copy_to_host() is not const.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_ppm(ImageType &im, const std::string &filename) {
    return Internal::save_pnm<ImageType, check>(im, 3, filename);
}

#ifndef HALIDE_NO_JPEG

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load_jpg(const std::string &filename, ImageType *im) {
    static_assert(!ImageType::has_static_halide_type, "");

    Internal::FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File could not be opened for reading")) {
        return false;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f.f);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    const int width = cinfo.output_width;
    const int height = cinfo.output_height;
    const int channels = cinfo.output_components;

    const halide_type_t im_type(halide_type_uint, 8);
    std::vector<int> im_dimensions = { width, height };
    if (channels > 1) {
        im_dimensions.push_back(channels);
    }
    *im = ImageType(im_type, im_dimensions);

    auto copy_to_image = Internal::read_big_endian_row<uint8_t, ImageType>;

    std::vector<uint8_t> row(width * channels);
    const int ymin = im->dim(1).min();
    const int ymax = im->dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        uint8_t *src = row.data();
        jpeg_read_scanlines(&cinfo, &src, 1);
        copy_to_image(row.data(), y, im);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return true;
}

inline const std::set<FormatInfo> &query_jpg() {
    static std::set<FormatInfo> info = {
        { halide_type_t(halide_type_uint, 8), 2 },
        { halide_type_t(halide_type_uint, 8), 3 },
    };
    return info;
}

template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_jpg(ImageType &im, const std::string &filename) {
    static_assert(!ImageType::has_static_halide_type, "");

    im.copy_to_host();

    const int width = im.width();
    const int height = im.height();
    const int channels = im.channels();
    if (!check(channels == 1 || channels == 3, "Wrong number of channels")) {
        return false;
    }

    Internal::FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File could not be opened for writing")) {
        return false;
    }

    // TODO: Make this an argument?
    constexpr int quality = 99;

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f.f);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = channels;
    cinfo.in_color_space = (channels == 3) ? JCS_RGB : JCS_GRAYSCALE;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    auto copy_from_image = Internal::write_big_endian_row<uint8_t, ImageType>;

    std::vector<uint8_t> row(width * channels);
    const int ymin = im.dim(1).min();
    const int ymax = im.dim(1).max();
    for (int y = ymin; y <= ymax; ++y) {
        uint8_t *dst = row.data();
        copy_from_image(im, y, dst);
        jpeg_write_scanlines(&cinfo, &dst, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return true;
}

#endif  // not HALIDE_NO_JPEG

constexpr int kNumTmpCodes = 10;

inline const halide_type_t *tmp_code_to_halide_type() {
    static const halide_type_t tmp_code_to_halide_type_[kNumTmpCodes] = {
      { halide_type_float, 32 },
      { halide_type_float, 64 },
      { halide_type_uint, 8 },
      { halide_type_int, 8 },
      { halide_type_uint, 16 },
      { halide_type_int, 16 },
      { halide_type_uint, 32 },
      { halide_type_int, 32 },
      { halide_type_uint, 64 },
      { halide_type_int, 64 }
    };
    return tmp_code_to_halide_type_;
}

// return true iff the buffer storage has no padding between
// any elements, and is in strictly planar order.
template<typename ImageType>
bool buffer_is_compact_planar(ImageType &im) {
    const halide_type_t im_type = im.type();
    const size_t elem_size = (im_type.bits / 8);
    if (((uint8_t*)im.begin() + (im.number_of_elements() * elem_size)) != (uint8_t*) im.end()) {
        return false;
    }
    for (int d = 1; d < im.dimensions(); ++d) {
        if (im.dim(d-1).stride() >= im.dim(d).stride()) {
            return false;
        }
    }
    return true;
}

// ".tmp" is a file format used by the ImageStack tool (see https://github.com/abadams/ImageStack)
template<typename ImageType, CheckFunc check = CheckReturn>
bool load_tmp(const std::string &filename, ImageType *im) {
    static_assert(!ImageType::has_static_halide_type, "");

    FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File could not be opened for reading")) {
        return false;
    }

    int32_t header[5];
    if (!check(f.read_array(header), "Count not read .tmp header")) {
        return false;
    }

    if (!check(header[0] > 0 && header[1] > 0 && header[2] > 0 && header[3] > 0 &&
               header[4] >= 0 && header[4] < kNumTmpCodes, "Bad header on .tmp file")) {
        return false;
    }

    const halide_type_t im_type = tmp_code_to_halide_type()[header[4]];
    std::vector<int> im_dimensions = { header[0], header[1], header[2], header[3] };
    *im = ImageType(im_type, im_dimensions);

    // This should never fail unless the default Buffer<> constructor behavior changes.
    if (!check(buffer_is_compact_planar(*im), "load_tmp() requires compact planar images")) {
        return false;
    }

    if (!check(f.read_bytes(im->begin(), im->size_in_bytes()), "Count not read .tmp payload")) {
        return false;
    }

    im->set_host_dirty();
    return true;
}

inline const std::set<FormatInfo> &query_tmp() {
    // TMP files require exactly 4 dimensions.
    static std::set<FormatInfo> info = {
      { halide_type_t(halide_type_float, 32), 4 },
      { halide_type_t(halide_type_float, 64), 4 },
      { halide_type_t(halide_type_uint, 8), 4 },
      { halide_type_t(halide_type_int, 8), 4 },
      { halide_type_t(halide_type_uint, 16), 4 },
      { halide_type_t(halide_type_int, 16), 4 },
      { halide_type_t(halide_type_uint, 32), 4 },
      { halide_type_t(halide_type_int, 32), 4 },
      { halide_type_t(halide_type_uint, 64), 4 },
      { halide_type_t(halide_type_int, 64), 4 },
    };
    return info;
}

template<typename ImageType, CheckFunc check = CheckReturn>
bool write_planar_payload(ImageType &im, FileOpener &f) {
    if (im.dimensions() == 0 || buffer_is_compact_planar(im)) {
        // Contiguous buffer! Write it all in one swell foop.
        if (!check(f.write_bytes(im.begin(), im.size_in_bytes()), "Count not write .tmp payload")) {
            return false;
        }
    } else {
        // We have to do this the hard way.
        int d = im.dimensions() - 1;
        for (int i = im.dim(d).min(); i <= im.dim(d).max(); i++) {
            ImageType slice = im.sliced(d, i);
            if (!write_planar_payload(slice, f)) {
                return false;
            }
        }
    }
    return true;
}

// ".tmp" is a file format used by the ImageStack tool (see https://github.com/abadams/ImageStack)
template<typename ImageType, CheckFunc check = CheckReturn>
bool save_tmp(ImageType &im, const std::string &filename) {
    static_assert(!ImageType::has_static_halide_type, "");

    im.copy_to_host();

    int32_t header[5] = { 1, 1, 1, 1, -1 };
    for (int i = 0; i < im.dimensions(); ++i) {
        header[i] = im.dim(i).extent();
    }
    auto *table = tmp_code_to_halide_type();
    for (int i = 0; i < kNumTmpCodes; i++) {
        if (im.type() == table[i]) {
            header[4] = i;
            break;
        }
    }
    if (!check(header[4] >= 0, "Unsupported type for .tmp file")) {
        return false;
    }

    FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File could not be opened for writing")) {
        return false;
    }
    if (!check(f.write_array(header), "Could not write .tmp header")) {
        return false;
    }

    if (!write_planar_payload<ImageType, check>(im, f)) {
        return false;
    }

    return true;
}


// ".mat" is the matlab level 5 format documented here:
// http://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf


enum MatlabTypeCode {
    miINT8 = 1,
    miUINT8 = 2,
    miINT16 = 3,
    miUINT16 = 4,
    miINT32 = 5,
    miUINT32 = 6,
    miSINGLE = 7,
    miDOUBLE = 9,
    miINT64 = 12,
    miUINT64 = 13,
    miMATRIX = 14,
    miCOMPRESSED = 15,
    miUTF8 = 16,
    miUTF16 = 17,
    miUTF32 = 18
};

enum MatlabClassCode {
    mxCHAR_CLASS = 3,
    mxDOUBLE_CLASS = 6,
    mxSINGLE_CLASS = 7,
    mxINT8_CLASS = 8,
    mxUINT8_CLASS = 9,
    mxINT16_CLASS = 10,
    mxUINT16_CLASS = 11,
    mxINT32_CLASS = 12,
    mxUINT32_CLASS = 13,
    mxINT64_CLASS = 14,
    mxUINT64_CLASS = 15
};

template<typename ImageType, CheckFunc check = CheckReturn>
bool load_mat(const std::string &filename, ImageType *im) {
    static_assert(!ImageType::has_static_halide_type, "");

    FileOpener f(filename, "rb");
    if (!check(f.f != nullptr, "File could not be opened for reading")) {
        return false;
    }

    uint8_t header[128];
    if (!check(f.read_array(header), "Could not read .mat header\n")) {
        return false;
    }

    // Matrix header
    uint32_t matrix_header[2];
    if (!check(f.read_array(matrix_header), "Could not read .mat header\n")) {
        return false;
    }
    if (!check(matrix_header[0] == miMATRIX, "Could not parse this .mat file: bad matrix header\n")) {
        return false;
    }

    // Array flags
    uint32_t flags[4];
    if (!check(f.read_array(flags), "Could not read .mat header\n")) {
        return false;
    }
    if (!check(flags[0] == miUINT32 && flags[1] == 8, "Could not parse this .mat file: bad flags\n")) {
        return false;
    }

    // Shape
    uint32_t shape_header[2];
    if (!check(f.read_array(shape_header), "Could not read .mat header\n")) {
        return false;
    }
    if (!check(shape_header[0] == miINT32, "Could not parse this .mat file: bad shape header\n")) {
        return false;
    }
    int dims = shape_header[1]/4;
    std::vector<int> extents(dims);
    if (!check(f.read_vector(&extents), "Could not read .mat header\n")) {
        return false;
    }
    if (dims & 1) {
        uint32_t padding;
        if (!check(f.read_bytes(&padding, 4), "Could not read .mat header\n")) {
            return false;
        }
    }

    // Skip over the name
    uint32_t name_header[2];
    if (!check(f.read_array(name_header), "Could not read .mat header\n")) {
        return false;
    }

    if (name_header[0] >> 16) {
        // Name must be fewer than 4 chars, and so the whole name
        // field was stored packed into 8 bytes
    } else {
        if (!check(name_header[0] == miINT8, "Could not parse this .mat file: bad name header\n")) {
            return false;
        }
        std::vector<uint64_t> scratch((name_header[1] + 7) / 8);
        if (!check(f.read_vector(&scratch), "Could not read .mat header\n")) {
            return false;
        }
    }

    // Payload header
    uint32_t payload_header[2];
    if (!check(f.read_array(payload_header), "Could not read .mat header\n")) {
        return false;
    }
    halide_type_t type;
    switch (payload_header[0]) {
    case miINT8:
        type = halide_type_of<int8_t>();
        break;
    case miINT16:
        type = halide_type_of<int16_t>();
        break;
    case miINT32:
        type = halide_type_of<int32_t>();
        break;
    case miINT64:
        type = halide_type_of<int64_t>();
        break;
    case miUINT8:
        type = halide_type_of<uint8_t>();
        break;
    case miUINT16:
        type = halide_type_of<uint16_t>();
        break;
    case miUINT32:
        type = halide_type_of<uint32_t>();
        break;
    case miUINT64:
        type = halide_type_of<uint64_t>();
        break;
    case miSINGLE:
        type = halide_type_of<float>();
        break;
    case miDOUBLE:
        type = halide_type_of<double>();
        break;
    }

    *im = ImageType(type, extents);

    // This should never fail unless the default Buffer<> constructor behavior changes.
    if (!check(buffer_is_compact_planar(*im), "load_mat() requires compact planar images")) {
        return false;
    }

    if (!check(f.read_bytes(im->begin(), im->size_in_bytes()), "Could not read .tmp payload")) {
        return false;
    }

    im->set_host_dirty();
    return true;
}

inline const std::set<FormatInfo> &query_mat() {
    // MAT files must have at least 2 dimensions, but there's no upper
    // bound. Our support arbitrarily stops at 16 dimensions.
    static std::set<FormatInfo> info = []() {
        std::set<FormatInfo> s;
        for (int i = 2; i < 16; i++) {
            s.insert({ halide_type_t(halide_type_float, 32), i });
            s.insert({ halide_type_t(halide_type_float, 64), i });
            s.insert({ halide_type_t(halide_type_uint, 8), i });
            s.insert({ halide_type_t(halide_type_int, 8), i });
            s.insert({ halide_type_t(halide_type_uint, 16), i });
            s.insert({ halide_type_t(halide_type_int, 16), i });
            s.insert({ halide_type_t(halide_type_uint, 32), i });
            s.insert({ halide_type_t(halide_type_int, 32), i });
            s.insert({ halide_type_t(halide_type_uint, 64), i });
            s.insert({ halide_type_t(halide_type_int, 64), i });
        }
        return s;
    }();
    return info;
}

template<typename ImageType, CheckFunc check = CheckReturn>
bool save_mat(ImageType &im, const std::string &filename) {
    static_assert(!ImageType::has_static_halide_type, "");

    im.copy_to_host();

    uint32_t class_code = 0, type_code = 0;
    switch (im.raw_buffer()->type.code) {
    case halide_type_int:
        switch (im.raw_buffer()->type.bits) {
        case 8:
            class_code = mxINT8_CLASS;
            type_code = miINT8;
            break;
        case 16:
            class_code = mxINT16_CLASS;
            type_code = miINT16;
            break;
        case 32:
            class_code = mxINT32_CLASS;
            type_code = miINT32;
            break;
        case 64:
            class_code = mxINT64_CLASS;
            type_code = miINT64;
            break;
        default:
            check(false, "unreachable");
        };
        break;
    case halide_type_uint:
        switch (im.raw_buffer()->type.bits) {
        case 8:
            class_code = mxUINT8_CLASS;
            type_code = miUINT8;
            break;
        case 16:
            class_code = mxUINT16_CLASS;
            type_code = miUINT16;
            break;
        case 32:
            class_code = mxUINT32_CLASS;
            type_code = miUINT32;
            break;
        case 64:
            class_code = mxUINT64_CLASS;
            type_code = miUINT64;
            break;
        default:
            check(false, "unreachable");
        };
        break;
    case halide_type_float:
        switch (im.raw_buffer()->type.bits) {
        case 32:
            class_code = mxSINGLE_CLASS;
            type_code = miSINGLE;
            break;
        case 64:
            class_code = mxDOUBLE_CLASS;
            type_code = miDOUBLE;
            break;
        default:
            check(false, "unreachable");
        };
        break;
    case halide_type_handle:
        check(false, "unreachable");
    }

    FileOpener f(filename, "wb");
    if (!check(f.f != nullptr, "File could not be opened for writing")) {
        return false;
    }

    // Pick a name for the array
    size_t idx = filename.rfind('.');
    std::string name = filename.substr(0, idx);
    idx = filename.rfind('/');
    if (idx != std::string::npos) {
        name = name.substr(idx+1);
    }

    // Matlab variable names conform to similar rules as C
    if (name.empty() || !std::isalpha(name[0])) {
        name = "v" + name;
    }
    for (size_t i = 0; i < name.size(); i++) {
        if (!std::isalnum(name[i])) {
            name[i] = '_';
        }
    }

    uint32_t name_size = (int)name.size();
    while (name.size() & 0x7) name += '\0';

    char header[128] = "MATLAB 5.0 MAT-file, produced by Halide";
    int len = strlen(header);
    memset(header + len, ' ', sizeof(header) - len);

    // Version
    *((uint16_t *)(header + 124)) = 0x0100;

    // Endianness check
    header[126] = 'I';
    header[127] = 'M';

    uint64_t payload_bytes = im.size_in_bytes();

    if (!check((payload_bytes >> 32) == 0, "Buffer too large to save as .mat")) {
        return false;
    }

    int dims = im.dimensions();
    if (dims < 2) {
        dims = 2;
    }
    int padded_dims = dims + (dims & 1);

    // Matrix header
    uint32_t matrix_header[2] = {
        miMATRIX, 40 + padded_dims * 4 + (uint32_t)name.size() + (uint32_t)payload_bytes
    };

    // Array flags
    uint32_t flags[4] = {
        miUINT32, 8, class_code, 1
    };

    // Shape
    int32_t shape[2] = {
        miINT32, im.dimensions() * 4,
    };
    std::vector<int> extents(im.dimensions());
    for (int d = 0; d < im.dimensions(); d++) {
        extents[d] = im.dim(d).extent();
    }
    while ((int)extents.size() < dims) {
        extents.push_back(1);
    }
    while ((int)extents.size() < padded_dims) {
        extents.push_back(0);
    }

    // Name
    uint32_t name_header[2] = {
        miINT8, name_size
    };

    uint32_t padding_bytes = 7 - ((payload_bytes - 1) & 7);

    // Payload header
    uint32_t payload_header[2] = {
        type_code, (uint32_t)payload_bytes
    };

    bool success =
        f.write_array(header) &&
        f.write_array(matrix_header) &&
        f.write_array(flags) &&
        f.write_array(shape) &&
        f.write_vector(extents) &&
        f.write_array(name_header) &&
        f.write_bytes(&name[0], name.size()) &&
        f.write_array(payload_header);

    if (!check(success, "Could not write .mat header")) {
        return false;
    }

    if (!write_planar_payload<ImageType, check>(im, f)) {
        return false;
    }

    // Padding
    if (!check(padding_bytes < 8, "Too much padding!\n")) {
        return false;
    }
    uint64_t padding = 0;
    if (!f.write_bytes(&padding, padding_bytes)) {
        return false;
    }

    return true;
}


template<typename ImageType, Internal::CheckFunc check>
struct ImageIO {
    std::function<bool(const std::string &, ImageType *)> load;
    std::function<bool(ImageType &im, const std::string &)> save;
    std::function<const std::set<FormatInfo>&()> query;
};

template<typename ImageType, Internal::CheckFunc check>
bool find_imageio(const std::string &filename, ImageIO<ImageType, check> *result) {
    static_assert(!ImageType::has_static_halide_type, "");

    const std::map<std::string, ImageIO<ImageType, check>> m = {
#ifndef HALIDE_NO_JPEG
        {"jpeg", {load_jpg<ImageType, check>, save_jpg<ImageType, check>, query_jpg}},
        {"jpg", {load_jpg<ImageType, check>, save_jpg<ImageType, check>, query_jpg}},
#endif
        {"pgm", {load_pgm<ImageType, check>, save_pgm<ImageType, check>, query_pgm}},
#ifndef HALIDE_NO_PNG
        {"png", {load_png<ImageType, check>, save_png<ImageType, check>, query_png}},
#endif
        {"ppm", {load_ppm<ImageType, check>, save_ppm<ImageType, check>, query_ppm}},
        {"tmp", {load_tmp<ImageType, check>, save_tmp<ImageType, check>, query_tmp}},
        {"mat", {load_mat<ImageType, check>, save_mat<ImageType, check>, query_mat}}
    };
    std::string ext = Internal::get_lowercase_extension(filename);
    auto it = m.find(ext);
    if (it != m.end()) {
        *result = it->second;
        return true;
    }

    std::string err = "unsupported file extension \"" + ext + "\", supported are:";
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

template<typename ImageType>
FormatInfo best_save_format(const ImageType &im, const std::set<FormatInfo> &info) {
    // A bit ad hoc, but will do for now:
    // Perfect score is zero (exact match).
    // The larger the score, the worse the match.
    int best_score = 0x7fffffff;
    FormatInfo best{};
    const halide_type_t im_type = im.type();
    const int im_dimensions = im.dimensions();
    for (auto &f : info) {
        int score = 0;
        // If format has too-few dimensions, that's very bad.
        score += std::max(0, im_dimensions - f.dimensions) * 1024;
        // If format has too-few bits, that's pretty bad.
        score += std::max(0, im_type.bits - f.type.bits) * 8;
        // If format has too-many bits, that's a little bad.
        score += std::max(0, f.type.bits - im_type.bits);
        // If format has different code, that's a little bad.
        score += (f.type.code != im_type.code) ? 1 : 0;
        if (score < best_score) {
            best_score = score;
            best = f;
        }
    }

    return best;
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
             typename std::enable_if<ImageType::has_static_halide_type && !std::is_void<DstElemType>::value>::type * = nullptr>
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
        dst.set_host_dirty();

        return dst;
    }

    // Convert an Image from one ElemType to another, where the dst type is statically
    // known but the src type is not (e.g. Buffer<> -> Buffer<float>).
    // You'd normally call this with an explicit type for DstElemType and
    // allow ImageType to be inferred, e.g.
    //     Buffer<uint8_t> src = ...;
    //     Buffer<float> dst = convert_image<float>(src);
    template<typename DstElemType, typename ImageType,
             typename std::enable_if<!ImageType::has_static_halide_type && !std::is_void<DstElemType>::value>::type * = nullptr>
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
                assert(false && "Unsupported type");
                using DstImageType = typename Internal::ImageTypeWithElemType<ImageType, DstElemType>::type;
                return DstImageType();
        }
    }

    // Convert an Image from one ElemType to another, where the src type
    // is statically known but the dst type is not
    // (e.g. Buffer<uint8_t> -> Buffer<>(halide_type_t)).
    template <typename DstElemType = void,
              typename ImageType,
              typename std::enable_if<ImageType::has_static_halide_type && std::is_void<DstElemType>::value>::type * = nullptr>
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
                assert(false && "Unsupported type");
                return ImageType();
        }
    }

    // Convert an Image from one ElemType to another, where neither src type
    // nor dst type are statically known
    // (e.g. Buffer<>(halide_type_t) -> Buffer<>(halide_type_t)).
    template <typename DstElemType = void,
              typename ImageType,
              typename std::enable_if<!ImageType::has_static_halide_type && std::is_void<DstElemType>::value>::type * = nullptr>
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
                assert(false && "Unsupported type");
                return ImageType();
        }
    }
};

// Load the Image from the given file.
// If output Image has a static type, and the loaded image cannot be stored
// in such an image without losing data, fail.
// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool load(const std::string &filename, ImageType *im) {
    using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
    Internal::ImageIO<DynamicImageType, check> imageio;
    if (!Internal::find_imageio<DynamicImageType, check>(filename, &imageio)) {
        return false;
    }
    using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
    DynamicImageType im_d;
    if (!imageio.load(filename, &im_d)) {
        return false;
    }
    // Allow statically-typed images to be passed as the out-param, but do
    // a runtime check to ensure
    if (ImageType::has_static_halide_type) {
        const halide_type_t expected_type = ImageType::static_halide_type();
        if (!check(im_d.type() == expected_type, "Image loaded did not match the expected type")) {
            return false;
        }
    }
    *im = im_d.template as<typename ImageType::ElemType>();
    im->set_host_dirty();
    return true;
}

// Save the Image in the format associated with the filename's extension.
// If the format can't represent the Image without losing data, fail.
// Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save(ImageType &im, const std::string &filename) {
    using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
    Internal::ImageIO<DynamicImageType, check> imageio;
    if (!Internal::find_imageio<DynamicImageType, check>(filename, &imageio)) {
        return false;
    }
    if (!check(imageio.query().count({im.type(), im.dimensions()}) > 0, "Image cannot be saved in this format")) {
        return false;
    }

    // Allow statically-typed images to be passed in, but quietly pass them on
    // as dynamically-typed images.
    auto im_d = im.template as<void>();
    return imageio.save(im_d, filename);
}

// Return a set of FormatInfo structs that contain the legal type-and-dimensions
// that can be saved in this format. Most applications won't ever need to use
// this call. Returns false upon failure.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckReturn>
bool save_query(const std::string &filename, std::set<FormatInfo> *info) {
    using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
    Internal::ImageIO<DynamicImageType, check> imageio;
    if (!Internal::find_imageio<DynamicImageType, check>(filename, &imageio)) {
        return false;
    }
    *info = imageio.query();
    return true;
}

// Fancy wrapper to call load() with CheckFail, inferring the return type;
// this allows you to simply use
//
//    Image im = load_image("filename");
//
// without bothering to check error results (all errors simply abort).
//
// Note that if the image being loaded doesn't match the static type and
// dimensions of of the image on the LHS, a runtime error will occur.
class load_image {
public:
    load_image(const std::string &f) : filename(f) {}

    template<typename ImageType>
    operator ImageType() {
        using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
        DynamicImageType im_d;
        (void) load<DynamicImageType, Internal::CheckFail>(filename, &im_d);
        return im_d.template as<typename ImageType::ElemType>();
    }

private:
  const std::string filename;
};

// Like load_image, but quietly convert the loaded image to the type of the LHS
// if necessary, discarding information if necessary.
class load_and_convert_image {
public:
    load_and_convert_image(const std::string &f) : filename(f) {}

    template<typename ImageType>
    inline operator ImageType() {
        using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
        DynamicImageType im_d;
        (void) load<DynamicImageType, Internal::CheckFail>(filename, &im_d);
        const halide_type_t expected_type = ImageType::static_halide_type();
        if (im_d.type() == expected_type) {
            return im_d.template as<typename ImageType::ElemType>();
        } else {
            return ImageTypeConversion::convert_image<typename ImageType::ElemType>(im_d);
        }
    }

private:
  const std::string filename;
};

// Fancy wrapper to call save() with CheckFail; this allows you to simply use
//
//    save_image(im, "filename");
//
// without bothering to check error results (all errors simply abort).
//
// If the specified image file format cannot represent the image without
// losing data (e.g, a float32 or 4-dimensional image saved as a JPEG),
// a runtime error will occur.
template<typename ImageType, Internal::CheckFunc check = Internal::CheckFail>
void save_image(ImageType &im, const std::string &filename) {
    (void) save<ImageType, check>(im, filename);
}

// Like save_image, but quietly convert the saved image to a type that the
// specified image file format can hold, discarding information if necessary.
// (Note that the input image is unaffected!)
template<typename ImageType, Internal::CheckFunc check = Internal::CheckFail>
void convert_and_save_image(ImageType &im, const std::string &filename) {
    std::set<FormatInfo> info;
    (void) save_query<ImageType, check>(filename, &info);
    const FormatInfo best = Internal::best_save_format(im, info);
    if (best.type == im.type() && best.dimensions == im.dimensions()) {
        // It's an exact match, we can save as-is.
        (void) save<ImageType, check>(im, filename);
    } else {
        using DynamicImageType = typename Internal::ImageTypeWithElemType<ImageType, void>::type;
        DynamicImageType im_converted = ImageTypeConversion::convert_image(im, best.type);
        while (im_converted.dimensions() < best.dimensions) {
            im_converted.add_dimension();
        }
        (void) save<DynamicImageType, check>(im_converted, filename);
    }
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_IMAGE_IO_H
