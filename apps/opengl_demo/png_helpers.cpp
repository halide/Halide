#include <cstdio>
#include <iostream>
#include <png.h>

#include "png_helpers.h"

using namespace PNGHelpers;

struct image_info PNGHelpers::load(const std::string &filepath) {
    const auto fp = fopen(filepath.c_str(), "rb");
    if (fp == 0) {
        perror(filepath.c_str());
        exit(1);
    }

    // verify the header
    png_byte header[8];
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        std::cerr << "error: " << filepath << " is not a PNG file." << std::endl;
        exit(1);
    }

    auto png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    auto png_info = png_create_info_struct(png);

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);  // already read header
    png_read_info(png, png_info);

    const auto width = png_get_image_width(png, png_info);
    const auto height = png_get_image_height(png, png_info);
    const auto color_type = png_get_color_type(png, png_info);
    const auto bit_depth = png_get_bit_depth(png, png_info);

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, png_info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, png_info);

    const auto rowbytes = png_get_rowbytes(png, png_info);
    const auto image_data = (png_byte *)malloc(rowbytes * height * sizeof(png_byte));

    const auto row_pointers = (png_byte **)malloc(height * sizeof(png_byte *));
    for (int i = 0; i < height; i++) {
        row_pointers[i] = image_data + i * rowbytes;
    }

    png_read_image(png, row_pointers);

    png_destroy_read_struct(&png, &png_info, nullptr);
    free(row_pointers);
    fclose(fp);

    return {width, height, image_data};
}
