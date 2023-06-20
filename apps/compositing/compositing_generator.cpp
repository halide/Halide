#include "Halide.h"

namespace {

constexpr int num_layers = 6;

class Compositing : public Halide::Generator<Compositing> {
    Var x{"x"}, y{"y"}, c{"c"};
public:
    Input<Buffer<uint8_t, 3>[num_layers]> layer_rgba{"layer_rgba"};
    Input<Buffer<int, 1>> ops{"ops"};
    Output<Buffer<uint8_t, 3>> output{"output"};

    Func extract_alpha(Func in) {
        Func alpha{in.name() + "_alpha"};
        alpha(x, y) = in(x, y, 3);
        return alpha;
    }
    
    Func premultiply_alpha(Func in) {
        Func out{in.name() + "_premul"};
        out(x, y, c) = widening_mul(in(x, y, c), in(x, y, 3));
        return out;
    }    
    
    Expr scale(Expr a, Expr b) {
        assert(b.type() == UInt(8));
        Expr c = widening_mul(a, cast(a.type(), b));
        //c = (c + 127) / 255;
        c += rounding_shift_right(c, 8);
        c = rounding_shift_right(c, 8);        
        return cast(a.type(), c);
    }
    
    struct RGBA {
        Expr rgb;
        Expr alpha;
    };
    
    RGBA over(const RGBA &a, const RGBA &b) {
        RGBA c;
        c.rgb = b.rgb + scale(a.rgb, ~b.alpha);
        c.alpha = b.alpha + scale(a.alpha, ~b.alpha);
        return c;
    }

    RGBA atop(const RGBA &a, const RGBA &b) {
        RGBA c;
        c.rgb = scale(b.rgb, a.alpha) + scale(a.rgb, ~b.alpha);
        c.alpha = a.alpha;
        return c;
    }

    RGBA xor_(const RGBA &a, const RGBA &b) {
        RGBA c;
        c.rgb = scale(b.rgb, ~a.alpha) + scale(a.rgb, ~b.alpha);
        c.alpha = scale(b.alpha, ~a.alpha) + scale(a.alpha, ~b.alpha);
        return c;
    }        

    RGBA in(const RGBA &a, const RGBA &b) {
        RGBA c;
        c.rgb = scale(a.rgb, b.alpha);
        c.alpha = scale(a.alpha, b.alpha);
        return c;
    }

    RGBA out(const RGBA &a, const RGBA &b) {
        RGBA c;
        c.rgb = scale(a.rgb, ~b.alpha);
        c.alpha = scale(a.alpha, ~b.alpha);
        return c;
    }    

    void generate() {
        // Convert everything to pre-multiplied alpha
        Func layer[num_layers], layer_alpha[num_layers];
        for (int i = 0; i < num_layers; i++) {
            layer[i] = premultiply_alpha(layer_rgba[i]);
            layer_alpha[i] = extract_alpha(layer_rgba[i]);
        }

        RGBA a {layer[0](x, y, c), layer_alpha[0](x, y)};
        for (int i = 1; i < num_layers; i++) {
            RGBA b {layer[i](x, y, c), layer_alpha[i](x, y)};
            RGBA blends[] = {over(a, b), atop(a, b), xor_(a, b), in(a, b), out(a, b)};
            a.rgb = mux(ops(i-1), {blends[0].rgb, blends[1].rgb, blends[2].rgb, blends[3].rgb, blends[4].rgb});
            a.alpha = mux(ops(i-1), {blends[0].alpha, blends[1].alpha, blends[2].alpha, blends[3].alpha, blends[4].alpha});            
        }

        output(x, y, c) = select(c < 3, cast<uint8_t>(min(255, fast_integer_divide(a.rgb + a.alpha/2,  a.alpha))),
                                 a.alpha);
        
        /* ESTIMATES */
        // TODO
        for (int i = 0; i < num_layers; i++)  {
            layer_rgba[i].set_estimates({{0, 1536}, {0, 2560}, {0, 4}});            
        }
        output.set_estimates({{0, 1536}, {0, 2560}, {0, 4}});
        ops.set_estimates({{0, num_layers-1}});
        
        /* THE SCHEDULE */
        if (using_autoscheduler()) {
            // Nothing.
        } else if (get_target().has_gpu_feature()) {
            // GPU schedule.
            // TODO
        } else {
            output.parallel(y, 8).vectorize(x, natural_vector_size<uint8_t>()).reorder(c, x, y).bound(c, 0, 4).unroll(c);
            // CPU schedule.
            // TODO

        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Compositing, compositing)
