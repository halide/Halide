// Test doesn't build on windows, because OpenGL on windows is a nightmare.
#ifdef _WIN32
#include <stdio.h>
int main() {
    printf("Skipping test on Windows\n");
    return 0;
}
#else // not _WIN32


#include <csetjmp>
#include <unistd.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "Halide.h"
#include "HalideRuntimeOpenGL.h"

std::string error_message;

/*
** Don't rely on func.set_error_handler() mechanism, it doesn't seem to catch
** the user OpenGL state errors. That might be a separate bug.
*/
jmp_buf env;
void on_sigabrt (int signum)
{
  longjmp (env, 1);
}
void catching_stderr_abort(std::function<void ()> func)
{
    auto ferr = tmpfile();
    auto prev_stderr = dup(2);
    dup2(fileno(ferr), 2);
    if (setjmp (env) == 0) {
        auto prev_handler = signal(SIGABRT, &on_sigabrt);
        (func)();
        signal(SIGABRT, prev_handler);
    }
    fseek(ferr, 0, SEEK_END);
    error_message.resize(ftell(ferr));
    rewind(ferr);
    fread(&error_message[0], 1, error_message.size(), ferr);
    dup2(prev_stderr, 2);
    close(prev_stderr);
}

using namespace Halide;

int main()
{
    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Image<uint8_t> output(255, 10, 3);

    Var x, y, c;
    Func g;
    g(x, y, c) = Halide::cast<uint8_t>(255);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);

    // Let Halide initialize OpenGL
    g.realize(output);

    // Bad OpenGL call leaves OpenGL in a bad state
    glEnableVertexAttribArray(-1);

    // Halide should report that the OpenGL context is in a bad state due to user code`
    catching_stderr_abort( [&] () { g.realize(output); } );

    if (error_message.empty()) {
        fprintf(stderr, "Failed to report error in user OpenGL state\n");
        return 1;
    }
    else if (error_message.find("user OpenGL state") == std::string::npos) {
        error_message.erase(error_message.size() - 1); // remove trailing newline
        fprintf(stderr, "Reported error '%s' rather than identifying error at 'user OpenGL state'\n", error_message.c_str());
        return 1;
    }

    printf("Success!\n");
    return 0;
}

#endif // not _WIN32
