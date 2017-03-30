#include "HalideRuntime.h"

// Use TIFF because it meets the following criteria:
// - Supports uncompressed data
// - Supports 3D images as well as 2D
// - Supports floating-point samples
// - Supports an arbitrary number of channels
// - Can be written with a reasonable amount of code in the runtime.
//   (E.g. this file instead of linking in another lib)
//
// It would be nice to use a format that web browsers read and display
// directly, but those formats don't tend to satisfy the above goals.

namespace Halide { namespace Runtime { namespace Internal {

// See "type_code" in DebugToFile.cpp
// TIFF sample type values are:
//     1 => Unsigned int
//     2 => Signed int
//     3 => Floating-point

WEAK int16_t pixel_type_to_tiff_sample_type[] = {
  3, 3, 1, 2, 1, 2, 1, 2, 1, 2
};

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

    void assign16(uint16_t tag_code, int32_t count, int16_t value) __attribute__((always_inline)) {
        this->tag_code = tag_code;
        this->type_code = 3;
        this->count = count;
        this->value.i16 = value;
    }

    void assign32(uint16_t tag_code, int32_t count, int32_t value) __attribute__((always_inline)) {
        this->tag_code = tag_code;
        this->type_code = 4;
        this->count = count;
        this->value.i32 = value;
    }

    void assign32(uint16_t tag_code, int16_t type_code, int32_t count, int32_t value)  __attribute__((always_inline)) {
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

WEAK bool has_tiff_extension(const char *filename) {
    const char *f = filename;

    while (*f != '\0') f++;
    while (f != filename && *f != '.') f--;

    if (*f != '.') return false;
    f++;

    if (*f != 't' && *f != 'T') return false;
    f++;

    if (*f != 'i' && *f != 'I') return false;
    f++;

    if (*f != 'f' && *f != 'F') return false;
    f++;

    if (*f == '\0') return true;

    if (*f != 'f' && *f != 'F') return false;
    f++;

    return *f == '\0';
}

// Get a pointer to the pixel data in a buffer, relative to the min
// coordinate, not in absolute coordinates.
WEAK uint8_t *get_pointer_to_data(int32_t dim0, int32_t dim1, int32_t dim2, int32_t dim3,
                                  const struct buffer_t *buf) {
    uint8_t *ptr = buf->host + buf->elem_size * (dim0 * buf->stride[0] +
                                                 dim1 * buf->stride[1] +
                                                 dim2 * buf->stride[2] +
                                                 dim3 * buf->stride[3]);
    return ptr;
}

}}} // namespace Halide::Runtime::Internal

WEAK extern "C" int32_t halide_debug_to_file(void *user_context, const char *filename,
                                             int32_t type_code, struct halide_buffer_t *buf) {

    if (buf->dimensions > 4) {
        halide_error(user_context, "Can't debug_to_file a Func with more than four dimensions\n");
        return -1;
    }

    halide_copy_to_host(user_context, buf);

    void *f = fopen(filename, "wb");
    if (!f) return -1;

    int32_t s0 = buf->dimensions > 0 ? buf->dim[0].extent : 1;
    int32_t s1 = buf->dimensions > 1 ? buf->dim[1].extent : 1;
    int32_t s2 = buf->dimensions > 2 ? buf->dim[2].extent : 1;
    int32_t s3 = buf->dimensions > 3 ? buf->dim[3].extent : 1;

    int32_t bytes_per_element = buf->type.bytes();

    size_t elts = s0;
    elts *= s1*s2*s3;

    if (has_tiff_extension(filename)) {
        int32_t channels;
        int32_t width = s0;
        int32_t height = s1;
        int32_t depth;

        if ((s3 == 0 || s3 == 1) && (s2 < 5)) {
            channels = s2;
            depth = 1;
        } else {
            channels = s3;
            depth = s2;
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
        tag++->assign32(256, 1, width);                          // Image width
        tag++->assign32(257, 1, height);                         // Image height
        tag++->assign16(258, 1, int16_t(bytes_per_element * 8)); // Bits per sample
        tag++->assign16(259, 1, 1);                              // Compression -- none
        tag++->assign16(262, 1, channels >= 3 ? 2 : 1);          // PhotometricInterpretation -- black is zero or RGB
        tag++->assign32(273, channels, sizeof(header));          // Rows per strip
        tag++->assign16(277, 1, int16_t(channels));              // Samples per pixel
        tag++->assign32(278, 1, s1);                             // Rows per strip
        tag++->assign32(279, channels,
                        (channels == 1) ?
                            elts * bytes_per_element :
                            sizeof(header) +
                                channels * sizeof(int32_t));     // strip byte counts, bug if 32-bit truncation
        tag++->assign32(282, 5, 1,
                        __builtin_offsetof(halide_tiff_header, width_resolution));     // Width resolution
        tag++->assign32(283, 5, 1,
                        __builtin_offsetof(halide_tiff_header, height_resolution));    // Height resolution
        tag++->assign16(284, 1, 2);                              // Planar configuration -- planar
        tag++->assign16(296, 1, 1);                              // Resolution Unit -- none
        tag++->assign16(339, 1,
                        pixel_type_to_tiff_sample_type[type_code]);        // Sample type
        tag++->assign32(32997, 1, depth);                        // Image depth

        header.ifd0_end = 0;
        header.width_resolution[0] = 1;
        header.width_resolution[1] = 1;
        header.height_resolution[0] = 1;
        header.height_resolution[1] = 1;

        if (!fwrite((void *)(&header), sizeof(header), 1, f)) {
            fclose(f);
            return -2;
        }

        if (channels > 1) {
          int32_t offset = sizeof(header) + channels * sizeof(int32_t) * 2;

          for (int32_t i = 0; i < channels; i++) {
              if (!fwrite((void*)(&offset), 4, 1, f)) {
                  fclose(f);
                  return -2;
              }
              offset += s0 * s1 * depth * bytes_per_element;
          }
          int32_t count = s0 * s1 * depth;
          for (int32_t i = 0; i < channels; i++) {
              if (!fwrite((void*)(&count), 4, 1, f)) {
                  fclose(f);
                  return -2;
              }
          }
        }
    } else {
        int32_t header[] = {s0, s1, s2, s3, type_code};
        if (!fwrite((void *)(&header[0]), sizeof(header), 1, f)) {
            fclose(f);
            return -2;
        }
    }

    // Reorder the data according to the strides.
    const int TEMP_SIZE = 4*1024;
    uint8_t temp[TEMP_SIZE];
    int max_elts = TEMP_SIZE/bytes_per_element;
    int counter = 0;

    for (int32_t dim3 = 0; dim3 < s3; ++dim3) {
        for (int32_t dim2 = 0; dim2 < s2; ++dim2) {
            for (int32_t dim1 = 0; dim1 < s1; ++dim1) {
                for (int32_t dim0 = 0; dim0 < s0; ++dim0) {
                    counter++;
                    int idx[] = {dim0, dim1, dim2, dim3};
                    uint8_t *loc = buf->address_of(idx);
                    void *dst = temp + (counter-1)*bytes_per_element;
                    memcpy(dst, loc, bytes_per_element);

                    if (counter == max_elts) {
                        counter = 0;
                        if (!fwrite((void *)temp, max_elts*bytes_per_element, 1, f)) {
                            fclose(f);
                            return -1;
                        }
                    }
                }
            }
        }
    }
    if (counter > 0) {
        if (!fwrite((void *)temp, counter*bytes_per_element, 1, f)) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    return 0;
}
