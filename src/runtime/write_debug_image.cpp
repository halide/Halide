#include "HalideRuntime.h"

// We support four formats, npy, tiff, mat, and tmp.
//
// All formats support arbitrary types, and are easy to write in a
// small amount of code.
//
// npy:
// - Arbitrary dimensionality, type
// - Readable by NumPy and other Python tools
// TIFF:
// - 2/3-D only
// - Readable by a lot of tools
// mat:
// - Arbitrary dimensionality, type
// - Readable by matlab, ImageStack, and many other tools
// tmp:
// - Dirt simple, easy to roll your own parser
// - Readable by ImageStack only
// - Will probably be deprecated in favor of .mat soon
//
// It would be nice to use a format that web browsers read and display
// directly, but those formats don't tend to satisfy the above goals.

namespace Halide {
namespace Runtime {
namespace Internal {

// Mappings from the type_code passed in to the type codes of the
// formats. See "type_code" in DebugToFile.cpp

constexpr int kNumTypeCodes = 11;

// TIFF sample type values are:
//     1 => Unsigned int
//     2 => Signed int
//     3 => Floating-point
WEAK int16_t pixel_type_to_tiff_sample_type[kNumTypeCodes] = {
    // float, double, uint8, int8, ... uint64, int64
    3, 3, 1, 2, 1, 2, 1, 2, 1, 2, 0};

// See the .mat level 5 documentation for matlab class codes.
WEAK uint8_t pixel_type_to_matlab_class_code[kNumTypeCodes] = {
    7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 0};

WEAK uint8_t pixel_type_to_matlab_type_code[kNumTypeCodes] = {
    7, 9, 2, 1, 4, 3, 6, 5, 13, 12, 0};

#pragma pack(push)
#pragma pack(2)

struct tiff_tag {
    uint16_t tag_code;
    int16_t type_code;
    int32_t count;
    union {
        int8_t i8;
        int16_t i16;
        int32_t i32;
    } value;

    ALWAYS_INLINE void assign16(uint16_t tag_code, int32_t count, int16_t value) {
        this->tag_code = tag_code;
        this->type_code = 3;
        this->count = count;
        this->value.i16 = value;
    }

    ALWAYS_INLINE void assign32(uint16_t tag_code, int32_t count, int32_t value) {
        this->tag_code = tag_code;
        this->type_code = 4;
        this->count = count;
        this->value.i32 = value;
    }

    ALWAYS_INLINE void assign32(uint16_t tag_code, int16_t type_code, int32_t count, int32_t value) {
        this->tag_code = tag_code;
        this->type_code = type_code;
        this->count = count;
        this->value.i32 = value;
    }
};

struct halide_tiff_header {
    int16_t byte_order_marker;
    int16_t version;
    int32_t ifd0_offset;
    int16_t entry_count;
    tiff_tag entries[15];
    int32_t ifd0_end;
    int32_t width_resolution[2];
    int32_t height_resolution[2];
};

#pragma pack(pop)

WEAK bool ends_with(const char *filename, const char *suffix) {
    const char *f = filename, *s = suffix;
    while (*f) {
        f++;
    }
    while (*s) {
        s++;
    }
    while (s != suffix && f != filename) {
        if (*f != *s) {
            return false;
        }
        f--;
        s--;
    }
    return *f == *s;
}

struct ScopedFile {
    void *f;
    ALWAYS_INLINE ScopedFile(const char *filename, const char *mode) {
        f = halide_fopen(filename, mode);
    }
    ALWAYS_INLINE ~ScopedFile() {
        if (f) {
            fclose(f);
        }
    }
    ALWAYS_INLINE bool write(const void *ptr, size_t bytes) {
        return f ? fwrite(ptr, bytes, 1, f) > 0 : false;
    }
    ALWAYS_INLINE bool open() const {
        return f != nullptr;
    }
};

// Halide runtime has lots of assumptions that we are always little-endian,
// so we'll hardcode this here; leaving in the logic to make it clear.
constexpr bool host_is_big_endian = false;
constexpr char little_endian_char = '<';
constexpr char big_endian_char = '>';
constexpr char no_endian_char = '|';
constexpr char host_endian_char = (host_is_big_endian ? big_endian_char : little_endian_char);

struct npy_dtype_info_t {
    char byte_order;
    char kind;
    size_t item_size;
};

struct htype_to_dtype {
    halide_type_t htype;
    npy_dtype_info_t dtype;
};

WEAK htype_to_dtype npy_dtypes[] = {
    {halide_type_t(halide_type_float, 16), {host_endian_char, 'f', 2}},
    {halide_type_of<float>(), {host_endian_char, 'f', sizeof(float)}},
    {halide_type_of<double>(), {host_endian_char, 'f', sizeof(double)}},
    {halide_type_of<int8_t>(), {no_endian_char, 'i', sizeof(int8_t)}},
    {halide_type_of<int16_t>(), {host_endian_char, 'i', sizeof(int16_t)}},
    {halide_type_of<int32_t>(), {host_endian_char, 'i', sizeof(int32_t)}},
    {halide_type_of<int64_t>(), {host_endian_char, 'i', sizeof(int64_t)}},
    {halide_type_of<uint8_t>(), {no_endian_char, 'u', sizeof(uint8_t)}},
    {halide_type_of<uint16_t>(), {host_endian_char, 'u', sizeof(uint16_t)}},
    {halide_type_of<uint32_t>(), {host_endian_char, 'u', sizeof(uint32_t)}},
    {halide_type_of<uint64_t>(), {host_endian_char, 'u', sizeof(uint64_t)}},
};

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

WEAK extern "C" int halide_debug_to_file(void *user_context, const char *filename,
                                         int32_t type_code, struct halide_buffer_t *buf) {

    if (buf->is_bounds_query()) {
        halide_error(user_context, "Bounds query buffer passed to halide_debug_to_file");
        return halide_error_code_host_is_null;
    }

    if (buf->dimensions > 4) {
        halide_error(user_context, "Can't debug_to_file a Func with more than four dimensions\n");
        return halide_error_code_bad_dimensions;
    }

    if (auto result = halide_copy_to_host(user_context, buf); result != halide_error_code_success) {
        // halide_error() has already been called
        return result;
    }

    // Note: all calls to this function are wrapped in an assert that identifies
    // the function that failed, so calling halide_error() anywhere after this is redundant
    // and actually unhelpful.

    ScopedFile f(filename, "wb");
    if (!f.open()) {
        return halide_error_code_debug_to_file_failed;
    }

    size_t elts = 1;
    halide_dimension_t shape[4];
    for (int i = 0; i < buf->dimensions && i < 4; i++) {
        shape[i] = buf->dim[i];
        elts *= shape[i].extent;
    }
    for (int i = buf->dimensions; i < 4; i++) {
        shape[i].min = 0;
        shape[i].extent = 1;
        shape[i].stride = 0;
    }
    int32_t bytes_per_element = buf->type.bytes();

    uint32_t final_padding_bytes = 0;

    if (ends_with(filename, ".npy")) {
        npy_dtype_info_t di = {0, 0, 0};
        for (const auto &d : npy_dtypes) {
            if (d.htype == buf->type) {
                di = d.dtype;
                break;
            }
        }
        if (di.byte_order == 0) {
            return halide_error_code_debug_to_file_failed;
        }

        constexpr int max_dict_string_size = 1024;
        char dict_string_buf[max_dict_string_size];
        char *dst = dict_string_buf;
        char *end = dict_string_buf + max_dict_string_size - 1;

        dst = halide_string_to_string(dst, end, "{'descr': '");
        *dst++ = di.byte_order;
        *dst++ = di.kind;
        dst = halide_int64_to_string(dst, end, di.item_size, 1);
        dst = halide_string_to_string(dst, end, "', 'fortran_order': False, 'shape': (");
        for (int d = 0; d < buf->dimensions; ++d) {
            if (d > 0) {
                dst = halide_string_to_string(dst, end, ",");
            }
            dst = halide_int64_to_string(dst, end, buf->dim[d].extent, 1);
            if (buf->dimensions == 1) {
                dst = halide_string_to_string(dst, end, ",");  // special-case for single-element tuples
            }
        }
        dst = halide_string_to_string(dst, end, ")}\n");
        if (dst >= end) {
            // bloody unlikely, but just in case
            return halide_error_code_debug_to_file_failed;
        }

        const char *npy_magic_string_and_version = "\x93NUMPY\x01\x00";

        const size_t unpadded_length = 8 + 2 + (dst - dict_string_buf);
        const size_t padded_length = (unpadded_length + 64 - 1) & ~(64 - 1);
        const size_t padding = padded_length - unpadded_length;
        memset(dst, ' ', padding);
        dst += padding;

        const size_t header_len = dst - dict_string_buf;
        if (header_len > 65535) {
            return halide_error_code_debug_to_file_failed;
        }
        const uint8_t header_len_le[2] = {
            (uint8_t)((header_len >> 0) & 0xff),
            (uint8_t)((header_len >> 8) & 0xff)};

        if (!f.write(npy_magic_string_and_version, 8)) {
            return halide_error_code_debug_to_file_failed;
        }
        if (!f.write(header_len_le, 2)) {
            return halide_error_code_debug_to_file_failed;
        }
        if (!f.write(dict_string_buf, dst - dict_string_buf)) {
            return halide_error_code_debug_to_file_failed;
        }
    } else if (ends_with(filename, ".tiff") || ends_with(filename, ".tif")) {
        if (type_code == 10) {
            return halide_error_code_debug_to_file_failed;
        }

        int32_t channels;
        int32_t width = shape[0].extent;
        int32_t height = shape[1].extent;
        int32_t depth;

        if ((shape[3].extent == 0 || shape[3].extent == 1) && (shape[2].extent < 5)) {
            channels = shape[2].extent;
            depth = 1;
        } else {
            channels = shape[3].extent;
            depth = shape[2].extent;
        }

        struct halide_tiff_header header;

        int32_t MMII = 0x4d4d4949;
        // Select the appropriate two bytes signaling byte order automatically
        const char *c = (const char *)&MMII;
        header.byte_order_marker = (c[0] << 8) | c[1];

        header.version = 42;
        header.ifd0_offset = __builtin_offsetof(halide_tiff_header, entry_count);
        header.entry_count = sizeof(header.entries) / sizeof(header.entries[0]);

        tiff_tag *tag = &header.entries[0];
        tag++->assign32(256, 1, width);                           // Image width
        tag++->assign32(257, 1, height);                          // Image height
        tag++->assign16(258, 1, int16_t(bytes_per_element * 8));  // Bits per sample
        tag++->assign16(259, 1, 1);                               // Compression -- none
        tag++->assign16(262, 1, channels >= 3 ? 2 : 1);           // PhotometricInterpretation -- black is zero or RGB
        tag++->assign32(273, channels, sizeof(header));           // Rows per strip
        tag++->assign16(277, 1, int16_t(channels));               // Samples per pixel
        tag++->assign32(278, 1, shape[1].extent);                 // Rows per strip
        tag++->assign32(279, channels,
                        (channels == 1) ?
                            elts * bytes_per_element :
                            sizeof(header) +
                                channels * sizeof(int32_t));  // strip byte counts, bug if 32-bit truncation
        tag++->assign32(282, 5, 1,
                        __builtin_offsetof(halide_tiff_header, width_resolution));  // Width resolution
        tag++->assign32(283, 5, 1,
                        __builtin_offsetof(halide_tiff_header, height_resolution));  // Height resolution
        tag++->assign16(284, 1, 2);                                                  // Planar configuration -- planar
        tag++->assign16(296, 1, 1);                                                  // Resolution Unit -- none
        tag++->assign16(339, 1,
                        pixel_type_to_tiff_sample_type[type_code]);  // Sample type
        tag++->assign32(32997, 1, depth);                            // Image depth

        header.ifd0_end = 0;
        header.width_resolution[0] = 1;
        header.width_resolution[1] = 1;
        header.height_resolution[0] = 1;
        header.height_resolution[1] = 1;

        if (!f.write((void *)(&header), sizeof(header))) {
            return halide_error_code_debug_to_file_failed;
        }

        if (channels > 1) {
            int32_t offset = sizeof(header) + channels * sizeof(int32_t) * 2;

            for (int32_t i = 0; i < channels; i++) {
                if (!f.write((void *)(&offset), 4)) {
                    return halide_error_code_debug_to_file_failed;
                }
                offset += shape[0].extent * shape[1].extent * depth * bytes_per_element;
            }
            int32_t count = shape[0].extent * shape[1].extent * depth * bytes_per_element;
            for (int32_t i = 0; i < channels; i++) {
                if (!f.write((void *)(&count), 4)) {
                    return halide_error_code_debug_to_file_failed;
                }
            }
        }
    } else if (ends_with(filename, ".mat")) {
        if (type_code == 10) {
            return halide_error_code_debug_to_file_failed;
        }

        // Construct a name for the array from the filename
        const char *end = filename;
        while (*end) {
            end++;
        }
        while (*end != '.') {
            end--;
        }
        const char *start = end;
        while (start != filename && start[-1] != '/') {
            start--;
        }
        uint32_t name_size = (uint32_t)(end - start);
        char array_name[256];
        char *dst = array_name;
        while (start != end) {
            *dst++ = *start++;
        }
        while (dst < array_name + sizeof(array_name)) {
            *dst++ = 0;
        }

        uint32_t padded_name_size = (name_size + 7) & ~7;

        char header[129] =
            "MATLAB 5.0 MAT-file, produced by Halide                         "
            "                                                            \000\001IM";

        f.write(header, 128);

        size_t payload_bytes = buf->size_in_bytes();
        final_padding_bytes = 7 - ((payload_bytes - 1) & 7);

        // level 5 .mat files have a size limit. (Padding itself should never cause the overflow.
        // Code written this way for safety.)
        if (((uint64_t)payload_bytes + final_padding_bytes) >> 32) {
            return halide_error_code_debug_to_file_failed;
        }

        int dims = buf->dimensions;
        // .mat files require at least two dimensions
        if (dims < 2) {
            dims = 2;
        }

        int padded_dimensions = (dims + 1) & ~1;

        uint32_t tags[] = {
            // This is a matrix
            14, 40 + padded_dimensions * 4 + padded_name_size + (uint32_t)payload_bytes + final_padding_bytes,
            // The element type
            6, 8, pixel_type_to_matlab_class_code[type_code], 1,
            // The shape
            5, (uint32_t)(dims * 4)};

        if (!f.write(&tags, sizeof(tags))) {
            return halide_error_code_debug_to_file_failed;
        }

        int extents[] = {shape[0].extent, shape[1].extent, shape[2].extent, shape[3].extent};
        if (!f.write(&extents, padded_dimensions * 4)) {
            return halide_error_code_debug_to_file_failed;
        }

        // The name
        uint32_t name_header[2] = {1, name_size};
        if (!f.write(&name_header, sizeof(name_header))) {
            return halide_error_code_debug_to_file_failed;
        }

        if (!f.write(array_name, padded_name_size)) {
            return halide_error_code_debug_to_file_failed;
        }

        // Payload header
        uint32_t payload_header[2] = {
            pixel_type_to_matlab_type_code[type_code], (uint32_t)payload_bytes};
        if (!f.write(payload_header, sizeof(payload_header))) {
            return halide_error_code_debug_to_file_failed;
        }
    } else {
        if (type_code == 10) {
            return halide_error_code_debug_to_file_failed;
        }

        int32_t header[] = {shape[0].extent,
                            shape[1].extent,
                            shape[2].extent,
                            shape[3].extent,
                            type_code};
        if (!f.write((void *)(&header[0]), sizeof(header))) {
            return halide_error_code_debug_to_file_failed;
        }
    }

    // Reorder the data according to the strides.
    const int TEMP_SIZE = 4 * 1024;
    uint8_t temp[TEMP_SIZE];
    int max_elts = TEMP_SIZE / bytes_per_element;
    int counter = 0;

    for (int32_t dim3 = shape[3].min; dim3 < shape[3].extent + shape[3].min; ++dim3) {
        for (int32_t dim2 = shape[2].min; dim2 < shape[2].extent + shape[2].min; ++dim2) {
            for (int32_t dim1 = shape[1].min; dim1 < shape[1].extent + shape[1].min; ++dim1) {
                for (int32_t dim0 = shape[0].min; dim0 < shape[0].extent + shape[0].min; ++dim0) {
                    counter++;
                    int idx[] = {dim0, dim1, dim2, dim3};
                    uint8_t *loc = buf->address_of(idx);
                    void *dst = temp + (counter - 1) * bytes_per_element;
                    memcpy(dst, loc, bytes_per_element);

                    if (counter == max_elts) {
                        counter = 0;
                        if (!f.write((void *)temp, max_elts * bytes_per_element)) {
                            return halide_error_code_debug_to_file_failed;
                        }
                    }
                }
            }
        }
    }
    if (counter > 0) {
        if (!f.write((void *)temp, counter * bytes_per_element)) {
            return halide_error_code_debug_to_file_failed;
        }
    }

    const uint64_t zero = 0;
    if (final_padding_bytes) {
        if (final_padding_bytes > sizeof(zero)) {
            return halide_error_code_debug_to_file_failed;
        }
        if (!f.write(&zero, final_padding_bytes)) {
            return halide_error_code_debug_to_file_failed;
        }
    }

    return halide_error_code_success;
}
