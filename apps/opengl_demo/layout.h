#ifndef _LAYOUT_HELPERS_H_
#define _LAYOUT_HELPERS_H_

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace Layout {

enum location { UL,
                UR,
                LL,
                LR };

struct info {
    int window_width;
    int window_height;
};

const struct info &setup(int image_width, int image_height);

void draw_image(enum location location, const uint8_t *data, int width, int height, const std::string &label);
void draw_texture(enum location location, GLuint texture_id, int width, int height, const std::string &label);
}  // namespace Layout

#endif
