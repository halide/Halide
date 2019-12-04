#include "glfw_helpers.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

using namespace GlfwHelpers;

static GLFWwindow *window;

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static void error_callback(int error, const char *description) {
    die(description);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
}

static bool first_focus = false;
static void focus_callback(GLFWwindow *window, int) {
    first_focus = true;
}

struct info GlfwHelpers::setup(int width, int height) {
    struct info info;

    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) die("couldn't init glfw!");
    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);  // Single buffer mode, to avoid any doublebuffering timing issues
    window = glfwCreateWindow(width, height, "opengl_halide_test", NULL, NULL);
    if (!window) die("couldn't create window!");
    glfwSetKeyCallback(window, key_callback);
    glfwSetWindowFocusCallback(window, focus_callback);
    glfwMakeContextCurrent(window);

    while (!first_focus) {
        glfwWaitEvents();
    }

    int framebuffer_width, framebuffer_height;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    info.dpi_scale = float(framebuffer_width) / float(width);

    return info;
}

void GlfwHelpers::terminate() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
}

void GlfwHelpers::set_opengl_context() {
    glfwMakeContextCurrent(window);
}
