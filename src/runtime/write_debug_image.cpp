#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <memory.h>
#include <string.h>

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

namespace {

// See "type_code" in DebugToFile.cpp
// TIFF sample type values are:
//     1 => Unsigned int
//     2 => Signed int
//     3 => Floating-point

int16_t pixel_type_to_tiff_sample_type[] = {
  3, 3, 1, 2, 1, 2, 1, 2, 1, 2
};

#pragma pack(push)
#pragma pack(2)

struct tiff_tag {
    int16_t tag_code;
    int16_t type_code;
    int32_t count;
    union {
        int8_t i8;
        int16_t i16;
        int32_t i32;
    } value;

  void assign16(int16_t tag_code, int32_t count, int16_t value) {
    this->tag_code = tag_code;
    this->type_code = 3;
    this->count = count;
    this->value.i16 = value;
  }

  void assign32(int16_t tag_code, int32_t count, int32_t value) {
    this->tag_code = tag_code;
    this->type_code = 4;
    this->count = count;
    this->value.i32 = value;
  }

  void assign32(int16_t tag_code, int16_t type_code, int32_t count, int32_t value) {
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

bool has_tiff_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    return (dot != NULL                &&
	    (  tolower(dot[1]) ==  't' &&
	       tolower(dot[2]) ==  'i' &&
	       tolower(dot[3]) ==  'f' &&
	     ( tolower(dot[4]) == '\0' ||
	      (tolower(dot[4]) ==  'f' &&
	       tolower(dot[5]) == '\0'))));
}

typedef bool (*write_func)(const void *bytes, size_t size, void *arg);

int halide_write_debug_image(const char *filename, uint8_t *data,
			    int32_t s0, int32_t s1, int32_t s2, int32_t s3, 
			    int32_t type_code, int32_t bytes_per_element, write_func write, void* write_arg) {
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
	memcpy(&header.byte_order_marker, &MMII, 2);

	header.version = 42;
	header.ifd0_offset = offsetof(halide_tiff_header, entry_count);
	header.entry_count = sizeof(header.entries) / sizeof(header.entries[0]);

	tiff_tag *tag = &header.entries[0];
	tag++->assign32(256, 1, s0);                             // Image width
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
		      offsetof(halide_tiff_header, width_resolution));     // Width resolution
	tag++->assign32(283, 5, 1,
		      offsetof(halide_tiff_header, height_resolution));    // Height resolution
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

	if(!(*write)((void *)(&header), sizeof(header), write_arg))
	    return -2;

	if (channels > 1) {
	  int32_t offset = sizeof(header) + channels * sizeof(int32_t) * 2;

	  for (int32_t i = 0; i < channels; i++) {
	    if (!(*write)((void*)(&offset), 4, write_arg))
		return -2;
	    offset += s0 * s1 * depth * bytes_per_element;
	  }
	  int32_t count = s0 * s1 * depth;
	  for (int32_t i = 0; i < channels; i++) {
	    if (!(*write)((void*)(&count), 4, write_arg))
		return -2;
	  }
	}
    }
    else {
        int32_t header[] = {s0, s1, s2, s3, type_code};
        if(!(*write)((void *)(&header[0]), sizeof(header), write_arg))
            return -2;
    }

    bool write_success = (*write)((void *)data, bytes_per_element * elts, write_arg);
    if (!write_success)
        return -1;
    return 0;
    }      
  
};
