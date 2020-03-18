#ifndef _OPENGL_HELPERS_H_
#define _OPENGL_HELPERS_H_

#include <string>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace OpenGLHelpers {
void setup(float dpi_scale);
GLuint create_texture(int width, int height, const uint8_t *data);
void delete_texture(GLuint texture_id);
void display_texture(GLuint texture_id, float x0, float x1, float y0, float y1);
void draw_text(const std::string &text, float x, float y);
}  // namespace OpenGLHelpers

#endif
