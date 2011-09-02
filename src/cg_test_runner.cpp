/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Trivial test harness for cg_test.ml.
 * Compile together with generated asm:
 *
 *  $ ./cg_test.native                                  --> cg_test.bc 
 *  $ llc cg_test.bc                                    --> cg_test.s
 *  $ g++ cg_test_runner.cpp cg_test.s -lpng -o my_test --> my_test
 *  $ ./my_test [in.png]                                --> out_my_test.png
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <png.h>

extern "C" {
    void _im_main_runner(char* args[]);
}

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif //PATH_MAX

png_byte* malloc_aligned(size_t bytes) {
    //size_t mem = (size_t)malloc(bytes*2);
    //mem = mem & 0xFFFFFFFFFFFFFFF;
    void* mem;
    assert( posix_memalign(&mem, 16, bytes) == 0 );
    return (png_byte*)mem;
}

// returned bytes are allocated by load_png, owned by caller
bool load_png(const char* filename, int* w, int* h, int* channels, png_byte** data);
bool save_png(const char* filename, int width, int height, int channels, const png_byte* data);

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.png>\n", argv[0]);
        exit(1);
    }

    char outpath[PATH_MAX];

    const char* inpath = argv[1];
    //snprintf(outpath, PATH_MAX, "out_%s.png", argv[0]);
    snprintf(outpath, PATH_MAX, "out.png", argv[0]);

    printf("in: %s, out: %s\n", inpath, outpath);

    unsigned char *in, *out;
    int width, height, channels;
    
    if (!load_png(inpath, &width, &height, &channels, &in)) {
        fprintf(stderr, "Error loading '%s'\n", inpath);
        exit(1);
    }

    out = (unsigned char*)malloc_aligned(width*height*channels);

    printf("running...\n");
    char* args[] = {(char*)in, (char*)out, (char*)width, (char*)height, (char*)channels};
    _im_main_runner(args);

    save_png(outpath, width, height, channels, out);

    return 0;
}

#define check(cond, msg) \
    if (!(cond)) {fprintf(stderr, (msg)); return false;}

bool load_png(const char* filename, int* w, int* h, int* ch, png_byte** bytes) {
    png_byte header[8];	// 8 is the maximum size that can be checked
    png_structp png_ptr;
    png_infop info_ptr;
    int number_of_passes;
    png_bytep * row_pointers;
    
    /* open file and test for it being a png */
    FILE *f = fopen(filename, "rb");
    check(f, "[load_png] File could not be opened for reading\n");
    fread(header, 1, 8, f);
    check(!png_sig_cmp(header, 0, 8), "[load_png] File is not recognized as a PNG file\n");
    
    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    
    check(png_ptr, "[load_png] png_create_read_struct failed\n");
    
    info_ptr = png_create_info_struct(png_ptr);
    check(info_ptr, "[load_png] png_create_info_struct failed\n");
    
    check(!setjmp(png_jmpbuf(png_ptr)), "[load_png] Error during init_io\n");
    
    png_init_io(png_ptr, f);
    png_set_sig_bytes(png_ptr, 8);
    
    png_read_info(png_ptr, info_ptr);
    
    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // Expand low-bpp images to have only 1 pixel per byte (As opposed to tight packing)
    if (bit_depth < 8)
        png_set_packing(png_ptr);

    check(bit_depth <= 8, "Can't handle pngs with a bit depth greater than 8\n");

    png_byte *data = malloc_aligned(width*height*channels);
    fprintf(stderr, "malloc png data: %p\n", data);

    number_of_passes = png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);
    
    // read the file
    check(!setjmp(png_jmpbuf(png_ptr)), "[load_png] Error during read_image\n");
    
    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++)
        row_pointers[y] = (png_byte*)malloc(sizeof(png_byte)*png_get_rowbytes(png_ptr, info_ptr));
    
    png_read_image(png_ptr, row_pointers);
    
    fclose(f);
    
    // copy the data into the image buffer
    
    for (int y = 0; y < height; y++) {
        png_bytep srcPtr = row_pointers[y];
        for (int x = 0; x < width; x++) {
            for (int c = 0; c < channels; c++) {
                data[(c*height + y)*width + x] = *srcPtr++;
            }
        }
    }

    // clean up
    for (int y = 0; y < height; y++)
        free(row_pointers[y]);
    free(row_pointers);
    
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    // Return results
    *bytes = data;
    *w = width;
    *h = height;
    *ch = channels;

    return true;
}

bool save_png(const char* filename, int width, int height, int channels, const png_byte* data) {
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep * row_pointers;
    png_byte color_type;

    check(channels > 0 && channels < 5, 
          "[save_png] Can't write PNG files that have other than 1, 2, 3, or 4 channels\n");

    png_byte color_types[4] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA, 
                               PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA};
    color_type = color_types[channels - 1];
    
    // open file
    FILE *f = fopen(filename, "wb");
    check(f, "[save_png] File could not be opened for writing\n");
    
    // initialize stuff
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    check(png_ptr, "[save_png] png_create_write_struct failed\n");
    
    info_ptr = png_create_info_struct(png_ptr);
    check(info_ptr, "[save_png] png_create_info_struct failed\n");
    
    check(!setjmp(png_jmpbuf(png_ptr)), "[save_png] Error during init_io\n");
    
    png_init_io(png_ptr, f);
    
    // write header
    check(!setjmp(png_jmpbuf(png_ptr)), "[save_png] Error during writing header\n");
    
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    
    png_write_info(png_ptr, info_ptr);
    
    // convert the bigarray to png bytes
    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte*)malloc(sizeof(png_byte)*png_get_rowbytes(png_ptr, info_ptr));
        png_bytep dstPtr = row_pointers[y];
        for (int x = 0; x < width; x++) {
            for (int c = 0; c < channels; c++) {
                *dstPtr++ = data[(c*height + y)*width + x];
            }
        }
    }
    
    // write data
    check(!setjmp(png_jmpbuf(png_ptr)), "[save_png] Error during writing bytes");
    
    png_write_image(png_ptr, row_pointers);
    
    // finish write
    check(!setjmp(png_jmpbuf(png_ptr)), "[save_png] Error during end of write");
    
    png_write_end(png_ptr, NULL);
    
    // clean up
    for (int y = 0; y < height; y++)
	    free(row_pointers[y]);
	free(row_pointers);
    
    fclose(f);
    
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return true;
}

