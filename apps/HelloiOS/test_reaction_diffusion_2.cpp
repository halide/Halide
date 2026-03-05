#include <iostream>
#include <cstdlib>
#include <cstring>
#include "HalideRuntime.h"
#include "HalideBuffer.h"

// Generated headers
#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_update.h"
#include "reaction_diffusion_2_render.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int width = 128;
    const int height = 128;

    try {
        std::cout << "Testing HelloiOS reaction-diffusion generators..." << std::endl;

        // Allocate buffers
        Buffer<float, 3> state(width, height, 3);
        // Rendered buffer must be interleaved (channels in innermost dimension)
        Buffer<uint8_t, 3> rendered = Buffer<uint8_t>::make_interleaved(width, height, 4);

        // Test init generator
        std::cout << "  Testing reaction_diffusion_2_init..." << std::endl;
        int result = reaction_diffusion_2_init(state);
        if (result != 0) {
            std::cerr << "    ERROR: init failed with code " << result << std::endl;
            return 1;
        }
        std::cout << "    ✓ init passed" << std::endl;

        // Test update generator (run a few iterations)
        std::cout << "  Testing reaction_diffusion_2_update..." << std::endl;
        int mouse_x = width / 2;
        int mouse_y = height / 2;
        int frame = 0;
        for (int i = 0; i < 10; i++) {
            result = reaction_diffusion_2_update(state, mouse_x, mouse_y, frame, state);
            if (result != 0) {
                std::cerr << "    ERROR: update failed at iteration " << i
                          << " with code " << result << std::endl;
                return 1;
            }
            frame++;
        }
        std::cout << "    ✓ update passed (10 iterations)" << std::endl;

        // Test render generator
        std::cout << "  Testing reaction_diffusion_2_render..." << std::endl;
        int output_bgra = 0;  // Use RGBA format for testing
        result = reaction_diffusion_2_render(state, output_bgra, rendered);
        if (result != 0) {
            std::cerr << "    ERROR: render failed with code " << result << std::endl;
            return 1;
        }
        std::cout << "    ✓ render passed" << std::endl;

        // Sanity check: verify rendered output has some non-zero values
        bool has_data = false;
        for (int y = 0; y < height && !has_data; y++) {
            for (int x = 0; x < width && !has_data; x++) {
                if (rendered(x, y, 0) != 0 || rendered(x, y, 1) != 0 || rendered(x, y, 2) != 0) {
                    has_data = true;
                }
            }
        }
        if (!has_data) {
            std::cerr << "    WARNING: Rendered output is all zeros (unexpected)" << std::endl;
        }

        std::cout << "✅ All HelloiOS generators passed!" << std::endl;
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception caught: " << e.what() << std::endl;
        return 1;
    }
}
