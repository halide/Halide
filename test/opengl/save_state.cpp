// Test doesn't build on windows, because OpenGL on windows is a nightmare.
#ifdef _WIN32
#include <stdio.h>
int main() {
    printf("Skipping test on Windows\n");
    return 0;
}
#else

#include <cstring>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "Halide.h"

#include "runtime/mini_opengl.h"

extern "C" void glGenTextures(GLsizei, GLuint *);
extern "C" void glTexParameteri(GLenum, GLenum, GLint);
extern "C" void glBindTexture(GLenum, GLuint);
extern "C" void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
extern "C" GLuint glCreateProgram();
extern "C" void glAttachShader(GLuint, GLuint);
extern "C" void glLinkProgram(GLuint);
extern "C" void glGetProgramiv(GLuint, GLenum, GLint *);
extern "C" void glGetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
extern "C" GLuint glCreateShader(GLenum);
extern "C" void glShaderSource(GLuint, GLsizei, const GLchar **, const GLint *);
extern "C" void glCompileShader(GLuint);
extern "C" void glGetShaderiv(GLuint, GLenum, GLint *);
extern "C" void glGetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
extern "C" void glEnable(GLenum);
extern "C" void glDisable(GLenum);
extern "C" void glGetIntegerv(GLenum, GLint *);
extern "C" void glGetBooleanv(GLenum, GLboolean *);
extern "C" GLenum glGetError();
extern "C" void glActiveTexture(GLenum);
extern "C" void glEnableVertexAttribArray(GLuint);
extern "C" void glDisableVertexAttribArray(GLuint);
extern "C" void glUseProgram(GLuint);
extern "C" void glGenBuffers(GLsizei, GLuint *);
extern "C" void glViewport(GLint, GLint, GLsizei, GLsizei);
extern "C" void glGenFramebuffers(GLsizei, GLuint *);
extern "C" void glBindBuffer(GLenum, GLuint);
extern "C" void glBindFramebuffer(GLenum, GLuint);
extern "C" void glGenVertexArrays(GLsizei, GLuint *);
extern "C" void glBindVertexArray(GLuint);
extern "C" void glGetVertexAttribiv(GLuint, GLenum, GLint *);
extern "C" const GLubyte *glGetString(GLenum name);

// Generates an arbitrary program.
class Program {
public:
    static GLuint handle() {
        const char *vertexShader = " \
                                    attribute vec4 Position;  \
                                    attribute vec2 TexCoordIn; \
                                    varying vec2 TexCoordOut; \
                                    void main(void) {  \
                                        gl_Position = Position; \
                                        TexCoordOut = TexCoordIn; \
                                    }";

        const char *fragmentShader = " \
                                      varying vec2 TexCoordOut; \
                                      uniform sampler2D Texture; \
                                      void main(void) { \
                                          gl_FragColor = texture2D(Texture, TexCoordOut); \
                                      }";

        GLuint handle = glCreateProgram();
        glAttachShader(handle, compileShader("vertex", vertexShader, GL_VERTEX_SHADER));
        glAttachShader(handle, compileShader("fragment", fragmentShader, GL_FRAGMENT_SHADER));
        glLinkProgram(handle);

        GLint linkSuccess;
        glGetProgramiv(handle, GL_LINK_STATUS, &linkSuccess);
        if (linkSuccess == GL_FALSE) {
            GLchar messages[256];
            glGetProgramInfoLog(handle, sizeof(messages), 0, messages);
            fprintf(stderr, "Error linking program: %s\n", messages);
            exit(1);
        }

        return handle;
    }

private:
    static GLuint compileShader(const char *label, const char *shaderString, GLenum shaderType) {
        const GLuint handle = glCreateShader(shaderType);
        const int len = strlen(shaderString);
        glShaderSource(handle, 1, &shaderString, &len);
        glCompileShader(handle);
        GLint compileSuccess;
        glGetShaderiv(handle, GL_COMPILE_STATUS, &compileSuccess);
        if (compileSuccess == GL_FALSE) {
            GLchar messages[256];
            glGetShaderInfoLog(handle, sizeof(messages), 0, messages);
            fprintf(stderr, "Error compiling %s shader: %s\n", label, messages);
            exit(1);
        }
        return handle;
    }
};

// Encapsulates setting OpenGL's state to arbitrary values, and checking
// whether the state matches those values.
class KnownState {
private:
    void gl_enable(GLenum cap, bool state) {
        (state ? glEnable : glDisable)(cap);
    }

    GLuint gl_gen(void (*fn)(GLsizei, GLuint *)) {
        GLuint val;
        (*fn)(1, &val);
        return val;
    }

    void check_value(const char *operation, const char *label, GLenum pname, GLint initial) {
        GLint val;
        glGetIntegerv(pname, &val);
        if (val != initial) {
            fprintf(stderr, "%s did not restore %s: initial value was %d (%#x), current value is %d (%#x)\n", operation, label, initial, initial, val, val);
            errors = true;
        }
    }

    void check_value(const char *operation, const char *label, GLenum pname, GLenum initial) {
        check_value(operation, label, pname, (GLint)initial);
    }

    void check_value(const char *operation, const char *label, GLenum pname, GLint initial[], int n = 4) {
        GLint val[2048];
        glGetIntegerv(pname, val);
        for (int i = 0; i < n; i++) {
            if (val[i] != initial[i]) {
                fprintf(stderr, "%s did not restore %s: initial value was", operation, label);
                for (int j = 0; j < n; j++) {
                    fprintf(stderr, " %d", initial[j]);
                }
                fprintf(stderr, ", current value is");
                for (int j = 0; j < n; j++) {
                    fprintf(stderr, " %d", val[j]);
                }
                fprintf(stderr, "\n");
                errors = true;
                return;
            }
        }
    }

    void check_value(const char *operation, const char *label, GLenum pname, bool initial) {
        GLboolean val;
        glGetBooleanv(pname, &val);
        if (val != initial) {
            fprintf(stderr, "%s did not restore boolean %s: initial value was %s, current value is %s\n", operation, label, initial ? "true" : "false", val ? "true" : "false");
            errors = true;
        }
    }

    void check_error(const char *label) {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr, "Error setting %s: OpenGL error %#x\n", label, err);
            errors = true;
        }
    }

    // version of OpenGL
    int gl_major_version;
    int gl_minor_version;

    GLenum initial_active_texture;
    GLint initial_viewport[4];
    GLuint initial_array_buffer_binding;
    GLuint initial_element_array_buffer_binding;
    GLuint initial_current_program;
    GLuint initial_framebuffer_binding;
    static const int ntextures = 10;
    GLuint initial_bound_textures[ntextures];
    bool initial_cull_face;
    bool initial_depth_test;

    static const int nvertex_attribs = 10;
    bool initial_vertex_attrib_array_enabled[nvertex_attribs];

    // The next two functions are stolen from opengl.cpp
    // and are used to parse the major/minor version of OpenGL
    // to see if vertex array objects are supported
    const char *parse_int(const char *str, int *val) {
        int v = 0;
        size_t i = 0;
        while (str[i] >= '0' && str[i] <= '9') {
            v = 10 * v + (str[i] - '0');
            i++;
        }
        if (i > 0) {
            *val = v;
            return &str[i];
        }
        return NULL;
    }

    const char *parse_opengl_version(const char *str, int *major, int *minor) {
        str = parse_int(str, major);
        if (str == NULL || *str != '.') {
            return NULL;
        }
        return parse_int(str + 1, minor);
    }

    GLuint initial_vertex_array_binding;

public:
    bool errors{ false };

    // This sets most values to generated or arbitrary values, which the
    // halide calls would be unlikely to accidentally use.  But for boolean
    // values, we want to be sure that halide is really restoring the
    // initial value, not just setting it to true or false.  So we need to
    // be able to try both.
    void setup(bool boolval) {
        // parse the OpenGL version
        const char *version = (const char *)glGetString(GL_VERSION);
        parse_opengl_version(version, &gl_major_version, &gl_minor_version);

        glGenTextures(ntextures, initial_bound_textures);
        for (int i = 0; i < ntextures; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, initial_bound_textures[i]);
        }
        glActiveTexture(initial_active_texture = GL_TEXTURE3);

        for (int i = 0; i < nvertex_attribs; i++) {
            if ((initial_vertex_attrib_array_enabled[i] = boolval)) {
                glEnableVertexAttribArray(i);
            } else {
                glDisableVertexAttribArray(i);
            }
            char buf[256];
            sprintf(buf, "vertex attrib array %d state", i);
            check_error(buf);
        }

        glUseProgram(initial_current_program = Program::handle());
        glViewport(initial_viewport[0] = 111, initial_viewport[1] = 222, initial_viewport[2] = 333, initial_viewport[3] = 444);
        gl_enable(GL_CULL_FACE, initial_cull_face = boolval);
        gl_enable(GL_DEPTH_TEST, initial_depth_test = boolval);
        glBindBuffer(GL_ARRAY_BUFFER, initial_array_buffer_binding = gl_gen(glGenBuffers));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, initial_element_array_buffer_binding = gl_gen(glGenBuffers));
        glBindFramebuffer(GL_FRAMEBUFFER, initial_framebuffer_binding = gl_gen(glGenFramebuffers));

        // Vertex array objects are only used by Halide if the OpenGL version >=3
        if (gl_major_version >= 3) {
            glBindVertexArray(initial_vertex_array_binding = gl_gen(glGenVertexArrays));
        }

        check_error("known state");
    }

    void check(const char *operation) {
        check_value(operation, "ActiveTexture", GL_ACTIVE_TEXTURE, initial_active_texture);
        check_value(operation, "current program", GL_CURRENT_PROGRAM, initial_current_program);
        check_value(operation, "framebuffer binding", GL_FRAMEBUFFER_BINDING, initial_framebuffer_binding);
        check_value(operation, "array buffer binding", GL_ARRAY_BUFFER_BINDING, initial_array_buffer_binding);
        check_value(operation, "element array buffer binding", GL_ELEMENT_ARRAY_BUFFER_BINDING, initial_element_array_buffer_binding);
        check_value(operation, "viewport", GL_VIEWPORT, initial_viewport);
        check_value(operation, "GL_CULL_FACE", GL_CULL_FACE, initial_cull_face);
        check_value(operation, "GL_DEPTH_TEST", GL_DEPTH_TEST, initial_cull_face);

        // Vertex array objects are only used by Halide if the OpenGL version >=3
        if (gl_major_version >= 3) {
            check_value(operation, "vertex array binding", GL_VERTEX_ARRAY_BINDING, initial_vertex_array_binding);
        } else {
            fprintf(stderr, "Skipping vertex array binding tests because OpenGL version is %d.%d (<3.0)\n", gl_major_version, gl_minor_version);
        }

        for (int i = 0; i < ntextures; i++) {
            char buf[100];
            sprintf(buf, "bound texture (unit %d)", i);
            glActiveTexture(GL_TEXTURE0 + i);
            check_value(operation, buf, GL_TEXTURE_BINDING_2D, initial_bound_textures[i]);
        }

        for (int i = 0; i < nvertex_attribs; i++) {
            int initial = initial_vertex_attrib_array_enabled[i];
            GLint val;
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &val);
            if (val != initial) {
                fprintf(stderr, "%s did not restore boolean VertexAttributeArrayEnabled(%d): initial value was %s, current value is %s\n", operation, i, initial ? "true" : "false", val ? "true" : "false");
                errors = true;
            }
        }
    }
};

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    KnownState known_state;

    Buffer<uint8_t> input(255, 10, 3);
    Buffer<uint8_t> out(UInt(8), 255, 10, 3);

    Var x, y, c;
    Func g;
    g(x, y, c) = input(x, y, c);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);
    g.realize(out, target);  // let Halide initialize OpenGL

    known_state.setup(true);
    g.realize(out, target);
    known_state.check("realize");

    known_state.setup(true);
    out.copy_to_host();
    known_state.check("copy_to_host");

    known_state.setup(false);
    g.realize(out, target);
    known_state.check("realize");

    known_state.setup(false);
    out.copy_to_host();
    known_state.check("copy_to_host");

    if (known_state.errors) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}

#endif
