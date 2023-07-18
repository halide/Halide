#include "Halide.h"

namespace {

// This app does Porter-Duff compositing using a runtime-provided list of blend
// modes and layers. It demonstrates how to write a mini-interpreter in Halide
// that ingests some byte code to determine what to do. It also demonstrates
// some fixed-point math patterns useful in compositing.
class Compositing : public Halide::Generator<Compositing> {
    Var x{"x"}, y{"y"}, c{"c"};

    static constexpr int num_layers = 6;
    static constexpr int num_blend_modes = 5;

public:
    // A stack of RGBA layers to composite.
    Input<Buffer<uint8_t, 3>[num_layers]> layer_rgba { "layer_rgba" };

    // The blend modes to use for each layer after the first.
    Input<Buffer<int, 1>> ops{"ops"};

    // An RGBA output buffer.
    Output<Buffer<uint8_t, 3>> output{"output"};

    Tuple premultiply_alpha(Tuple in) {
        if (get_target().has_gpu_feature()) {
            const float scale = 1 / 255.0f;
            // Use floats on GPU for intermediates
            in[3] = in[3] * scale;
            for (int i = 0; i < 3; i++) {
                in[i] = in[i] * (scale * in[3]);
            }
        } else {
            // On CPU, use uint16s for color components with premultiplied alpha, and uint8 for alpha.
            for (int i = 0; i < 3; i++) {
                in[i] = widening_mul(in[i], in[3]);
            }
        }
        return in;
    }

    Tuple normalize(Tuple in) {
        if (in[0].type().is_float()) {
            for (int i = 0; i < 4; i++) {
                Expr scale = i < 3 ? 255.0f / in[3] : 255.0f;
                in[i] *= scale;
                in[i] = saturating_cast(UInt(8), in[i]);
            }
        } else {
            for (int i = 0; i < 3; i++) {
                in[i] = fast_integer_divide(in[i] + in[3] / 2, in[3]);
                in[i] = saturating_cast(UInt(8), in[i]);
            }
        }
        return in;
    }

    Expr scale(Expr a, Expr b) {
        if (a.type().is_float()) {
            return a * b;
        } else {
            assert(b.type() == UInt(8));
            Expr c = widening_mul(a, cast(a.type(), b));
            // The below is equivalent to c = (c + 127) / 255;
            c += rounding_shift_right(c, 8);
            c = rounding_shift_right(c, 8);
            return cast(a.type(), c);
        }
    }

    Expr invert(const Expr &e) {
        if (e.type().is_float()) {
            return 1 - e;
        } else {
            return ~e;
        }
    }

    // Various Porter-Duff blend modes, in terms of the operators above.
    Tuple over(const Tuple &a, const Tuple &b) {
        std::vector<Expr> c(4);
        for (int i = 0; i < 3; i++) {
            c[i] = b[i] + scale(a[i], invert(b[3]));
        }
        c[3] = b[3] + scale(a[3], invert(b[3]));
        return Tuple{c};
    }

    Tuple atop(const Tuple &a, const Tuple &b) {
        std::vector<Expr> c(4);
        for (int i = 0; i < 3; i++) {
            c[i] = scale(b[i], a[3]) + scale(a[i], invert(b[3]));
        }
        c[3] = a[3];
        return Tuple{c};
    }

    Tuple xor_(const Tuple &a, const Tuple &b) {
        std::vector<Expr> c(4);
        for (int i = 0; i < 3; i++) {
            c[i] = scale(b[i], invert(a[3])) + scale(a[i], invert(b[3]));
        }
        c[3] = scale(b[3], invert(a[3])) + scale(a[3], invert(b[3]));
        return Tuple{c};
    }

    Tuple in(const Tuple &a, const Tuple &b) {
        std::vector<Expr> c(4);
        for (int i = 0; i < 3; i++) {
            c[i] = scale(a[i], b[3]);
        }
        c[3] = scale(a[3], b[3]);
        return Tuple{c};
    }

    Tuple out(const Tuple &a, const Tuple &b) {
        std::vector<Expr> c(4);
        for (int i = 0; i < 3; i++) {
            c[i] = scale(a[i], invert(b[3]));
        }
        c[3] = scale(a[3], invert(b[3]));
        return Tuple{c};
    }

    void generate() {
        // RGB and alpha potentially have different types, so we store them as a Tuple
        std::vector<Tuple> layer_vec;
        for (int i = 0; i < num_layers; i++) {
            layer_vec.push_back({layer_rgba[i](x, y, 0), layer_rgba[i](x, y, 1), layer_rgba[i](x, y, 2), layer_rgba[i](x, y, 3)});
        }

        // Combine the separate layers into a single Func
        Func layer_muxed{"layer_muxed"};
        Var k{"k"};
        layer_muxed(x, y, k) = mux(k, layer_vec);

        // Convert to premultiplied alpha in the working type (float on GPU, uint16 on CPU)
        Func blended{"blended"};
        blended(x, y) = premultiply_alpha(layer_vec[0]);

        // We will perform all blend modes on all layers, and then use an
        // RDom::where clause to restrict it to the desired blend mode for each
        // layer. If we then unroll over r[0], this compiles to a switch
        // statement. It is a useful pattern for writing mini interpreters that
        // ingest a bytecode and use it to switch between various ops.
        RDom r(0, num_blend_modes, 0, num_layers - 1);
        r.where(r[0] == ops(r[1]));
        Tuple a = blended(x, y);
        Tuple b = premultiply_alpha(layer_muxed(x, y, r[1] + 1));
        std::vector<Tuple> blends = {over(a, b), atop(a, b), xor_(a, b), in(a, b), out(a, b)};
        blended(x, y) = mux(r[0], blends);

        // Divide by alpha and convert back to uint8.
        output(x, y, c) = mux(c, normalize(blended(x, y)));

        /* ESTIMATES */
        for (int i = 0; i < num_layers; i++) {
            layer_rgba[i].set_estimates({{0, 1536}, {0, 2560}, {0, 4}});
        }
        output.set_estimates({{0, 1536}, {0, 2560}, {0, 4}});
        ops.set_estimates({{0, num_layers - 1}});

        /* THE SCHEDULE */
        if (using_autoscheduler()) {
            // Nothing.
        } else if (get_target().has_gpu_feature()) {
            // GPU schedule. 2.4ms on an RTX 2060
            Var xi, yi;
            output.gpu_tile(x, y, xi, yi, 32, 8);
            blended.update().unroll(r[0]).unroll(r[1]);
        } else {
            // CPU schedule. 2ms on an i9-9960X at 3.1 GHz with 16 threads
            const int vec = natural_vector_size<uint8_t>();
            Var yo{"yo"}, yi{"yi"};
            output.split(y, yo, yi, 8)
                .parallel(yo)
                .vectorize(x, vec)
                .reorder(c, x, yi, yo)
                .bound(c, 0, 4)
                .unroll(c);

            // Compute the intermediate state per row of the output, so that our
            // switch over the op codes can be outside the loop over x.
            blended.store_in(MemoryType::Stack)
                .compute_at(output, yi)
                .vectorize(x, vec)
                .update()
                .reorder(x, r[0], r[1])
                // Unroll over the possible blend modes to get a switch statement.
                .unroll(r[0])
                // Unroll over layers to remove the mux in layer_muxed. Ideally
                // this wouldn't be necessary because LLVM should really convert
                // a select of loads of the same index into a select between the
                // base pointers hoisted outside of the inner loop, but
                // unfortunately it doesn't.
                .unroll(r[1])
                .vectorize(x, vec);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Compositing, compositing)
