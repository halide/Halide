#ifndef FIMAGE_PNG_H
#define FIMAGE_PNG_H

#include <png.h>
#include <string>
#include <stdio.h>

#define _assert(condition, ...) if (!(condition)) {fprintf(stderr, __VA_ARGS__); exit(-1);}

namespace FImage {

void convert(uint8_t in, uint8_t &out) {out = in;}
void convert(uint8_t in, uint16_t &out) {out = in << 8;}
void convert(uint8_t in, float &out) {out = in/255.0f;}
void convert(uint16_t in, uint8_t &out) {out = in >> 8;}
void convert(uint16_t in, uint16_t &out) {out = in;}
void convert(uint16_t in, float &out) {out = in/65535.0f;}
void convert(float in, uint8_t &out) {out = (uint8_t)(in*255.0f);}
void convert(float in, uint16_t &out) {out = (uint16_t)(in*65535.0f);}
void convert(float in, float &out) {out = in;}

template<typename T>
Image<T> load(std::string filename) {
    png_byte header[8]; 
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;

    /* open file and test for it being a png */
    FILE *f = fopen(filename.c_str(), "rb");
    _assert(f, "File %s could not be opened for reading\n", filename.c_str());
    _assert(fread(header, 1, 8, f) == 8, "File ended before end of header\n");
    _assert(!png_sig_cmp(header, 0, 8), "File %s is not recognized as a PNG file\n", filename.c_str());

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    _assert(png_ptr, "png_create_read_struct failed\n");

    info_ptr = png_create_info_struct(png_ptr);
    _assert(info_ptr, "png_create_info_struct failed\n");

    _assert(!setjmp(png_jmpbuf(png_ptr)), "Error during init_io\n");

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

    Image<T> im(width, height, channels);

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    // read the file
    _assert(!setjmp(png_jmpbuf(png_ptr)), "Error during read_image\n");

    row_pointers = new png_bytep[im.height()];
    for (int y = 0; y < im.height(); y++) {
        row_pointers[y] = new png_byte[png_get_rowbytes(png_ptr, info_ptr)];
    }

    png_read_image(png_ptr, row_pointers);

    fclose(f);

    _assert((bit_depth == 8) || (bit_depth == 16), "Can only handle 8-bit or 16-bit pngs\n");

    // convert the data to T
    if (bit_depth == 8) {
        for (int y = 0; y < im.height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers[y]);
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {                    
                    convert(*srcPtr++, im(x, y, c));
                }
            }
        }
    } else if (bit_depth == 16) {
        for (int y = 0; y < im.height(); y++) {
            uint8_t *srcPtr = (uint8_t *)(row_pointers[y]);
            for (int x = 0; x < im.width(); x++) {
                for (int c = 0; c < im.channels(); c++) {
                    uint16_t hi = (*srcPtr++) << 8;
                    uint16_t lo = hi | (*srcPtr++);
                    convert(lo, im(x, y, c));
                }
            }
        }
    }

    // clean up
    for (int y = 0; y < im.height(); y++) {
        delete[] row_pointers[y];
    }
    delete[] row_pointers;

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return im;
} 

template<typename T>
void save(Image<T> im, std::string filename) {
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    png_byte color_type;

    _assert(im.channels() > 0 && im.channels() < 5,
           "Can't write PNG files that have other than 1, 2, 3, or 4 channels\n");

    png_byte color_types[4] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
                               PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA
                              };
    color_type = color_types[im.channels() - 1];

    // open file
    FILE *f = fopen(filename.c_str(), "wb");
    _assert(f, "[write_png_file] File %s could not be opened for writing\n", filename.c_str());

    // initialize stuff
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    _assert(png_ptr, "[write_png_file] png_create_write_struct failed\n");

    info_ptr = png_create_info_struct(png_ptr);
    _assert(info_ptr, "[write_png_file] png_create_info_struct failed\n");

    _assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during init_io\n");

    png_init_io(png_ptr, f);

    // write header
    _assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing header\n");

    png_set_IHDR(png_ptr, info_ptr, im.width(), im.height(),
                 16, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    // convert to uint16_t
    row_pointers = new png_bytep[im.height()];
    for (int y = 0; y < im.height(); y++) {
	row_pointers[y] = new png_byte[png_get_rowbytes(png_ptr, info_ptr)];
        uint8_t *dstPtr = (uint8_t *)(row_pointers[y]);
        for (int x = 0; x < im.width(); x++) {
            for (int c = 0; c < im.channels(); c++) {
                uint16_t out;
                convert(im(x, y, c), out);
                *dstPtr++ = out >> 8; 
                *dstPtr++ = out & 0xff;
            }
        }
    }

    // write data
    _assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during writing bytes");

    png_write_image(png_ptr, row_pointers);

    // finish write
    _assert(!setjmp(png_jmpbuf(png_ptr)), "[write_png_file] Error during end of write");

    png_write_end(png_ptr, NULL);

    // clean up
    for (int y = 0; y < im.height(); y++) {
        delete[] row_pointers[y];
    }
    delete[] row_pointers;

    fclose(f);

    png_destroy_write_struct(&png_ptr, &info_ptr);
}
}

#endif
