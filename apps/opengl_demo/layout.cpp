#include <string>

#include "opengl_helpers.h"

#include "layout.h"

using namespace Layout;

static const int border_sz = 10;  // pixels
static const int header_sz = 20;  // pixels

static struct info state;

const struct info &Layout::setup(int image_width, int image_height) {
    state.window_width = 2 * image_width + 3 * border_sz;
    state.window_height = 2 * image_height + border_sz + 2 * header_sz;
    return state;
}

void Layout::draw_texture(enum location location, GLuint texture_id, int width, int height, const std::string &label) {
    int x0, x1, y0, y1, lx, ly;
    switch (location) {  // set X coords
    case LL:
    case UL:
        x0 = border_sz;
        x1 = x0 + width;
        lx = x0 + 2;
        break;
    case LR:
    case UR:
        x1 = state.window_width - border_sz;
        x0 = x1 - width;
        lx = x0 + 2;
        break;
    }
    switch (location) {  // set Y coords
    case LL:
    case LR:
        y0 = header_sz;
        y1 = y0 + height;
        ly = 6;
        break;
    case UL:
    case UR:
        y1 = state.window_height - header_sz;
        y0 = y1 - height;
        ly = y1 + 6;
        break;
    }

    OpenGLHelpers::display_texture(texture_id, 2.0 * x0 / state.window_width - 1.0, 2.0 * x1 / state.window_width - 1.0, 2.0 * y0 / state.window_height - 1.0, 2.0 * y1 / state.window_height - 1.0);
    OpenGLHelpers::draw_text(label, 2.0 * lx / state.window_width - 1.0, 2.0 * ly / state.window_height - 1.0);
}

void Layout::draw_image(enum location location, const uint8_t *data, int width, int height, const std::string &label) {
    const auto texture_id = OpenGLHelpers::create_texture(width, height, data);
    draw_texture(location, texture_id, width, height, label);
    OpenGLHelpers::delete_texture(texture_id);
}
