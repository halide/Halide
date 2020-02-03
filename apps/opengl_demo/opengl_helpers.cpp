#include <drawtext.h>

#include "opengl_helpers.h"

using namespace OpenGLHelpers;

static const int font_size = 12;

void OpenGLHelpers::setup(float dpi_scale) {
    const int scaled_font_size = font_size * dpi_scale;
    dtx_use_font(dtx_open_font(DTX_FONT, scaled_font_size), scaled_font_size);
    glClear(GL_COLOR_BUFFER_BIT);
}

GLuint OpenGLHelpers::create_texture(int width, int height, const uint8_t *data) {
    GLuint texture_id;
    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return texture_id;
}

void OpenGLHelpers::delete_texture(GLuint texture_id) {
    glDeleteTextures(1, &texture_id);
}

void OpenGLHelpers::display_texture(GLuint texture_id, float x0, float x1, float y0, float y1) {
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2d(1, 0);
    glVertex2f(x1, y1);
    glTexCoord2d(0, 0);
    glVertex2f(x0, y1);
    glTexCoord2d(0, 1);
    glVertex2f(x0, y0);
    glTexCoord2d(1, 1);
    glVertex2f(x1, y0);
    glEnd();
    glFinish();
}

void OpenGLHelpers::draw_text(const std::string &text, float x, float y) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(x, y, 0);
    glColor3f(1, 1, 1);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glScalef(2.0f / viewport[2], 2.0f / viewport[3], 1);
    dtx_string(text.c_str());
    glFinish();
}
