#include "Halide.h"
#include <stdio.h>

using namespace Halide;

constexpr int w = 16;
constexpr int h = 16;

/* Halide uses fast-math by default. is_nan must either be used inside
 * strict_float or as a test on inputs produced outside of
 * Halide. Using it to test results produced by math inside Halide but
 * not using strict_float is unreliable. This test covers both of these cases. */

int check_nans(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            if ((x - y) < 0) {
                if (im(x, y) != 0.0f) {
                    printf("undetected Nan for sqrt(%d - %d)\n", x, y);
                    return 1;
                }
            } else {
                if (im(x, y) != 1.0f) {
                    printf("unexpected Nan for sqrt(%d - %d)\n", x, y);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int check_infs(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            float e = (float)(x - w / 2) / (float)(y - h / 2);
            if (std::isinf(e)) {
                if (im(x, y) != 1.0f) {
                    printf("undetected Inf for (%d-%d)/(%d-%d) -> %f\n", x, w / 2, y, h / 2, e);
                    return 1;
                }
            } else {
                if (im(x, y) != 0.0f) {
                    printf("unexpected Inf for (%d-%d)/(%d-%d) -> %f\n", x, w / 2, y, h / 2, e);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int check_finites(const Buffer<float> &im) {
    for (int x = 0; x < im.dim(0).extent(); x++) {
        for (int y = 0; y < im.dim(1).extent(); y++) {
            float e = (float)(x - w / 2) / (float)(y - h / 2);
            if (std::isfinite(e)) {
                if (im(x, y) != 1.0f) {
                    printf("undetected finite for (%d-%d)/(%d-%d) -> %f\n", x, w / 2, y, h / 2, e);
                    return 1;
                }
            } else {
                if (im(x, y) != 0.0f) {
                    printf("unexpected finite for (%d-%d)/(%d-%d) -> %f\n", x, w / 2, y, h / 2, e);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().has_feature(Target::WebGPU)) {
        printf("[SKIP] WebGPU does not reliably support isnan, isinf, or isfinite.\n");
        return 0;
    }

    // ---- is_nan()
    {
        Func f;
        Var x;
        Var y;

        Expr e = sqrt(x - y);
        f(x, y) = strict_float(select(is_nan(e), 0.0f, 1.0f));
        f.vectorize(x, 8);

        Buffer<float> im = f.realize({w, h});
        if (check_nans(im) != 0) {
            return 1;
        }
    }

    {
        Buffer<float> non_halide_produced(w, h);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                non_halide_produced(x, y) = sqrt(x - y);
            }
        }

        ImageParam in(Float(32), 2);
        Func f;
        Var x;
        Var y;

        f(x, y) = select(is_nan(in(x, y)), 0.0f, 1.0f);
        f.vectorize(x, 8);

        in.set(non_halide_produced);
        Buffer<float> im = f.realize({w, h});
        if (check_nans(im) != 0) {
            return 1;
        }
    }

    // ---- is_inf()
    {
        Func f;
        Var x;
        Var y;

        Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
        f(x, y) = strict_float(select(is_inf(e), 1.0f, 0.0f));
        f.vectorize(x, 8);

        Buffer<float> im = f.realize({w, h});
        if (check_infs(im) != 0) {
            return 1;
        }
    }

    {
        Buffer<float> non_halide_produced(w, h);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
            }
        }

        ImageParam in(Float(32), 2);
        Func f;
        Var x;
        Var y;

        f(x, y) = select(is_inf(in(x, y)), 1.0f, 0.0f);
        f.vectorize(x, 8);

        in.set(non_halide_produced);
        Buffer<float> im = f.realize({w, h});
        if (check_infs(im) != 0) {
            return 1;
        }
    }

    // ---- is_finite()
    {
        Func f;
        Var x;
        Var y;

        Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
        f(x, y) = strict_float(select(is_finite(e), 1.0f, 0.0f));
        f.vectorize(x, 8);

        Buffer<float> im = f.realize({w, h});
        if (check_finites(im) != 0) {
            return 1;
        }
    }

    {
        Buffer<float> non_halide_produced(w, h);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
            }
        }

        ImageParam in(Float(32), 2);
        Func f;
        Var x;
        Var y;

        f(x, y) = select(is_finite(in(x, y)), 1.0f, 0.0f);
        f.vectorize(x, 8);

        in.set(non_halide_produced);
        Buffer<float> im = f.realize({w, h});
        if (check_finites(im) != 0) {
            return 1;
        }
    }

    if (get_jit_target_from_environment().has_gpu_feature()) {
        // ---- is_nan()
        {
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            Expr e = sqrt(x - y);
            f(x, y) = strict_float(select(is_nan(e), 0.0f, 1.0f));
            f.gpu_tile(x, y, tx, ty, 8, 8);

            Buffer<float> im = f.realize({w, h});
            if (check_nans(im) != 0) {
                return 1;
            }
        }

        {
            Buffer<float> non_halide_produced(w, h);
            for (int x = 0; x < w; x++) {
                for (int y = 0; y < h; y++) {
                    non_halide_produced(x, y) = sqrt(x - y);
                }
            }

            ImageParam in(Float(32), 2);
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            f(x, y) = select(is_nan(in(x, y)), 0.0f, 1.0f);
            f.gpu_tile(x, y, tx, ty, 8, 8);

            in.set(non_halide_produced);
            Buffer<float> im = f.realize({w, h});
            if (check_nans(im) != 0) {
                return 1;
            }
        }

        // ---- is_inf()
        {
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
            f(x, y) = strict_float(select(is_inf(e), 1.0f, 0.0f));
            f.gpu_tile(x, y, tx, ty, 8, 8);

            Buffer<float> im = f.realize({w, h});
            if (check_infs(im) != 0) {
                return 1;
            }
        }

        {
            Buffer<float> non_halide_produced(w, h);
            for (int x = 0; x < w; x++) {
                for (int y = 0; y < h; y++) {
                    non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
                }
            }

            ImageParam in(Float(32), 2);
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            f(x, y) = select(is_inf(in(x, y)), 1.0f, 0.0f);
            f.gpu_tile(x, y, tx, ty, 8, 8);

            in.set(non_halide_produced);
            Buffer<float> im = f.realize({w, h});
            if (check_infs(im) != 0) {
                return 1;
            }
        }

        // ---- is_finite()
        {
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            Expr e = cast<float>(x - w / 2) / cast<float>(y - h / 2);
            f(x, y) = strict_float(select(is_finite(e), 1.0f, 0.0f));
            f.gpu_tile(x, y, tx, ty, 8, 8);

            Buffer<float> im = f.realize({w, h});
            if (check_finites(im) != 0) {
                return 1;
            }
        }

        {
            Buffer<float> non_halide_produced(w, h);
            for (int x = 0; x < w; x++) {
                for (int y = 0; y < h; y++) {
                    non_halide_produced(x, y) = (float)(x - w / 2) / (float)(y - h / 2);
                }
            }

            ImageParam in(Float(32), 2);
            Func f;
            Var x;
            Var y;
            Var tx, ty;

            f(x, y) = select(is_finite(in(x, y)), 1.0f, 0.0f);
            f.gpu_tile(x, y, tx, ty, 8, 8);

            in.set(non_halide_produced);
            Buffer<float> im = f.realize({w, h});
            if (check_finites(im) != 0) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
