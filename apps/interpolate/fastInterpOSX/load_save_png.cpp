#include "load_save_png.hpp"

#include <png.h>

#include <iostream>
#include <cassert>
#include <vector>

#define LOG_ERROR( X ) std::cout << X << std::endl

using std::vector;

bool load_png(std::string filename, unsigned int &width, unsigned int &height, vector< uint32_t > &data) {
	width = height = 0;
	data.clear();
	//..... load file ......
	//Load a png file, as per the libpng docs:
	FILE *fp = fopen(filename.c_str(), "rb");
	if (!fp) {
		LOG_ERROR("  cannot open file.");
		return false;
	}
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, (png_error_ptr)NULL, (png_error_ptr)NULL);
	if (!png) {
		LOG_ERROR("  cannot alloc read struct.");
		fclose(fp);
		return false;
	}
	png_infop info = png_create_info_struct(png);
	if (!info) {
		LOG_ERROR("  cannot alloc info struct.");
		png_destroy_read_struct(&png, (png_infopp)NULL, (png_infopp)NULL);
		fclose(fp);
		return false;
	}
	png_bytep *row_pointers = NULL;
	if (setjmp(png_jmpbuf(png))) {
		LOG_ERROR("  png interal error.");
		png_destroy_read_struct(&png, &info, (png_infopp)NULL);
		if (row_pointers != NULL) delete[] row_pointers;
		fclose(fp);
		data.clear();
		return false;
	}
	png_init_io(png, fp);
	png_read_info(png, info);
	unsigned int w = png_get_image_width(png, info);
	unsigned int h = png_get_image_height(png, info);
	if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY || png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (!(png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA))
		png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
	if (png_get_bit_depth(png, info) < 8)
		png_set_packing(png);
	if (png_get_bit_depth(png,info) == 16)
		png_set_strip_16(png);
	//Ok, should be 32-bit RGBA now.

	png_read_update_info(png, info);
	unsigned int rowbytes = png_get_rowbytes(png, info);
	//Make sure it's the format we think it is...
	assert(rowbytes == w*sizeof(uint32_t));

	data.resize(w*h);
	row_pointers = new png_bytep[h];
	for (unsigned int r = 0; r < h; ++r) {
		row_pointers[h-1-r] = (png_bytep)(&data[r*w]);
	}
	png_read_image(png, row_pointers);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	delete[] row_pointers;

	width = w;
	height = h;
	return true;
}


void save_png(std::string filename, unsigned int width, unsigned int height, uint32_t const *data) {
//After the libpng example.c
	FILE *fp = fopen(filename.c_str(), "wb");
	if (fp == NULL) {
		LOG_ERROR("Can't open " << filename << " for writing.");
		return;
	}

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		fclose(fp);
		LOG_ERROR("Can't create write struct.");
		return;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fclose(fp);
		png_destroy_write_struct(&png_ptr, NULL);
		LOG_ERROR("Can't craete info pointer");
		return;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		fclose(fp);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		LOG_ERROR("Error writing png.");
		return;
	}

	png_init_io(png_ptr, fp);
	png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(png_ptr, info_ptr);
	//png_set_swap_alpha(png_ptr) // might need?
	vector< png_bytep > row_pointers(height);
	for (unsigned int i = 0; i < height; ++i) {
		row_pointers[i] = (png_bytep)&(data[(height - i - 1) * width]);
	}
	png_write_image(png_ptr, &(row_pointers[0]));

	png_write_end(png_ptr, info_ptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(fp);

	return;
}
