#include "Halide.h"

using namespace Halide;

void swap(Buffer<uint8_t> &buf, int idx1, int idx2) {
    uint8_t tmp = buf(idx1);
    buf(idx1) = buf(idx2);
    buf(idx2) = tmp;
}

// Implements a simple scatter pipeline to make use of VTCM available on v65+
// Hexagon DSP.
template<typename DTYPE>
int test() {
    const int W = 128;
    const int H = 64;

    srand(time(0));
    // Separate channels for xCoord and yCoord for scatter.
    Buffer<uint8_t> x_idx(W);
    Buffer<uint8_t> y_idx(H);
    for (int x = 0; x < W; x++) {
        x_idx(x) = (uint8_t)x;
    }
    for (int x = 0; x < H; x++) {
        y_idx(x) = (uint8_t)x;
    }
    // Create a random permutation for x_idx and y_idx by randomly shuffling
    // elements. All indices should be unique for scatters to avoid race
    // conditions.
    for (int i = 0; i < 1000; i++) {
        swap(x_idx, rand() % W, rand() % W);
        swap(y_idx, rand() % H, rand() % H);
    }
    // Compute reference output image.
    DTYPE ref_out[H][W];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            ref_out[y][x] = DTYPE(19);
        }
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            ref_out[y_idx(y)][x_idx(x)] = DTYPE(x_idx(x)) + DTYPE(x);
        }
    }

    Var x, y;
    Func f, g;

    RDom r(0, W, 0, H);
    Expr xCoord = clamp(cast<int32_t>(x_idx(r.x)), 0, W - 1);
    Expr yCoord = clamp(cast<int32_t>(y_idx(r.y)), 0, H - 1);
    // Scatter values all over f
    f(x, y) = cast<DTYPE>(19);
    f(xCoord, yCoord) = cast<DTYPE>(x_idx(r.x)) + cast<DTYPE>(r.x);
    g(x, y) = f(x, y);

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::HVX)) {
        const int vector_size = target.has_feature(Target::HVX) ? 128 : 64;
        Var yi;

        f
            .compute_at(g, Var::outermost())
            .vectorize(x, vector_size / 2);

        f
            .update(0)
            .allow_race_conditions()
            .vectorize(r.x, vector_size / 2);

        g
            .hexagon()
            .split(y, y, yi, H / 2)
            .parallel(y)
            .vectorize(x, vector_size / 2);

        if (target.features_any_of({Target::HVX_v65, Target::HVX_v66})) {
            f.store_in(MemoryType::VTCM);
        }
    }

    Buffer<DTYPE> buf = g.realize({W, H});

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (buf(x, y) != ref_out[y][x]) {
                printf("output(%d, %d) = %d instead of %d\n", x, y, buf(x, y),
                       ref_out[x][y]);
                return false;
            }
        }
    }

    return true;
}

int main() {
    if (!get_jit_target_from_environment().has_feature(Target::HVX)) {
        printf("[SKIP] hexagon_scatter is only useful when targeting HVX.\n");
        return 0;
    }

    if (!test<uint16_t>() ||
        !test<int16_t>() ||
        !test<uint32_t>() ||
        !test<int32_t>()) return 1;
    printf("Success!\n");
    return 0;
}
