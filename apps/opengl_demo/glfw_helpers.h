#ifndef _GLFW_HELPERS_H_
#define _GLFW_HELPERS_H_

namespace GlfwHelpers {

struct info {
    float dpi_scale;
};

struct info setup(int width, int height);
void set_opengl_context();
void terminate();
}  // namespace GlfwHelpers

#endif
