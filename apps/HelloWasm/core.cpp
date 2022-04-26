// Adapted from Tim Hutton's MIT-licensed example code at: https://github.com/timhutton/sdl-canvas-wasm

#include <SDL2/SDL.h>
#include <cstdlib>
#include <emscripten.h>
#include <iomanip>
#include <sstream>

#include "HalideBuffer.h"
#include "reaction_diffusion_init.h"
#include "reaction_diffusion_render.h"
#include "reaction_diffusion_update.h"

const int W = 1024, H = 1024;

struct Context {
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *tex = nullptr;
    int iteration = 0;
    int threads = 0;

    double smoothed_runtime = 0;
    double smoothed_fps = 0;
    double smoothed_blit_time = 0;
    double last_frame_time = 0;

    Halide::Runtime::Buffer<float, 3> buf1;
    Halide::Runtime::Buffer<float, 3> buf2;
    Halide::Runtime::Buffer<uint32_t, 2> pixel_buf;
};

void mainloop(void *arg) {
    Context *ctx = static_cast<Context *>(arg);
    SDL_Renderer *renderer = ctx->renderer;

    // Grab mouse position somehow
    int mx = W / 2, my = H / 2;
    SDL_GetMouseState(&mx, &my);

    double t1 = emscripten_get_now();
    reaction_diffusion_update(ctx->buf1, mx, my, ctx->iteration, ctx->buf2);
    reaction_diffusion_render(ctx->buf2, ctx->pixel_buf);
    double t2 = emscripten_get_now();

    std::swap(ctx->buf1, ctx->buf2);

    SDL_UpdateTexture(ctx->tex, NULL, ctx->pixel_buf.data(), ctx->pixel_buf.dim(1).stride() * sizeof(uint32_t));

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, ctx->tex, NULL, NULL);
    SDL_RenderPresent(renderer);

    double t3 = emscripten_get_now();

    double this_runtime = (t2 - t1);
    double this_blit_time = (t3 - t2);
    double this_fps = 1000 / (t3 - ctx->last_frame_time);

    ctx->iteration++;
    if (ctx->iteration < 2) {
        ctx->smoothed_runtime = this_runtime;
        ctx->smoothed_fps = this_fps;
        ctx->smoothed_blit_time = this_blit_time;
    } else {
        ctx->smoothed_runtime = 0.9 * ctx->smoothed_runtime + 0.1 * this_runtime;
        ctx->smoothed_fps = 0.9 * ctx->smoothed_fps + 0.1 * this_fps;
        ctx->smoothed_blit_time = 0.9 * ctx->smoothed_blit_time + 0.1 * this_blit_time;
    }
    ctx->last_frame_time = t3;

    if ((ctx->iteration & 15) == 15) {
        char buf[1024] = {0};
        snprintf(buf, sizeof(buf),
                 "Time for Halide update + render: %0.2f ms<br>"
                 "Time for blit to framebuffer: %0.2f ms<br>"
                 "Frame rate: %2.0f fps",
                 ctx->smoothed_runtime, ctx->smoothed_blit_time, ctx->smoothed_fps);
        // Run some javascript inline to update the web-page
        EM_ASM({
            document.getElementById(UTF8ToString($0)).innerHTML = UTF8ToString($1);
        },
               "runtime", buf);

        // Read the threads slider from the UI
        int threads = EM_ASM_INT({
            return parseInt(document.getElementById("threads").value);
        });

        halide_set_num_threads(threads);
        if (threads != ctx->threads) {
            halide_shutdown_thread_pool();
            ctx->threads = threads;
        }
    }
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_CreateWindowAndRenderer(W, H, 0, &window, &renderer);

    Context ctx;
    ctx.renderer = renderer;
    ctx.buf1 = Halide::Runtime::Buffer<float, 3>(W, H, 3);
    ctx.buf2 = Halide::Runtime::Buffer<float, 3>(W, H, 3);
    ctx.pixel_buf = Halide::Runtime::Buffer<uint32_t, 2>(W, H);

    ctx.tex = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                W, H);

    // Read the initial thread count from the DOM
    int threads = EM_ASM_INT({
        return parseInt(document.getElementById("threads").value);
    });
    halide_set_num_threads(threads);

    reaction_diffusion_init(ctx.buf1);

    // call the function repeatedly
    const int simulate_infinite_loop = 1;

    // call the function as fast as the browser wants to render (typically 60fps)
    const int fps = -1;
    emscripten_set_main_loop_arg(mainloop, &ctx, fps, simulate_infinite_loop);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
