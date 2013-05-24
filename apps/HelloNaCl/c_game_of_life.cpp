// Reuse buffer_t from the Halide version so that they have the same interface
#include "halide_game_of_life.h"

// A low-level scalar C version to use for timing comparisons
extern "C" void c_game_of_life(buffer_t *in, buffer_t *out) {
    const int margin = out->min[1];
    const int width = in->extent[0];
    const int height = in->extent[1];
    for (int y = margin; y < height - margin; y++) {
        uint8_t *in_ptr = in->host + 4 * (in->stride[1] * y + in->stride[0] * margin);
        uint8_t *out_ptr = out->host + 4 * (out->stride[1] * (y - margin));
        for (int x = margin; x < width - margin; x++) {
            for (int c = 0; c < 3; c++) {
                // Read in the neighborhood
                bool alive = in_ptr[0];
                bool E = in_ptr[-4];
                bool W = in_ptr[4];
                bool N = in_ptr[-width*4];
                bool S = in_ptr[width*4];
                bool NE = in_ptr[-width*4-4];
                bool NW = in_ptr[-width*4+4];
                bool SE = in_ptr[width*4-4];
                bool SW = in_ptr[width*4+4];
             
                // Count the number of live neighbors
                int count = NE + N + NW + E + W + SE + S + SW;

                alive = (count == 2 && alive) || count == 3;
                *out_ptr = alive ? 255 : 0;

                in_ptr++;
                out_ptr++;
            }

            // Write 255 to the alpha channel
            *out_ptr++ = 255;
            in_ptr++;
        }
    }
}

