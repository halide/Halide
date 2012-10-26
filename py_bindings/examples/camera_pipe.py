
"Camera pipeline."

import sys; sys.path += ['..', '.']
builtin_min = min
from halide import *

int_t = Int(32)
float_t = Float(32)

OUT_DIMS = (2560, 1920, 3)

def filter_func(result_type=UInt(8), schedule=-1, use_uniforms=False):
    x, y, tx, ty, c = Var('x'), Var('y'), Var('tx'), Var('ty'), Var('c')
    counter_interleave_x = [0]
    counter_interleave_y = [0]
    
    def hot_pixel_suppression(input):
        a = max(max(input[x-2, y], input[x+2, y]),
                max(input[x, y-2], input[x, y+2]))
        b = min(min(input[x-2, y], input[x+2, y]),
                min(input[x, y-2], input[x, y+2]))
        
        denoised = Func('denoised')
        denoised[x, y] = clamp(input[x, y], b, a)

        return denoised

    def interleave_x(a, b):
        counter_interleave_x[0] += 1
        out = Func('interleave_x%d'%counter_interleave_x[0])
        out[x, y] = select((x%2)==0, a[x/2, y], b[x/2, y])
        return out

    def interleave_y(a, b):
        counter_interleave_y[0] += 1
        out = Func('interleave_y%d'%counter_interleave_y[0])
        out[x, y] = select((y%2)==0, a[x, y/2], b[x, y/2])
        return out

    def deinterleave(raw):
        # Deinterleave the color channels
        deinterleaved = Func('deinterleaved')

        deinterleaved[x, y, c] = select(c == 0, raw[2*x, 2*y], 
                                 select(c == 1, raw[2*x+1, 2*y],
                                 select(c == 2, raw[2*x, 2*y+1], 
                                                raw[2*x+1, 2*y+1])))
        return deinterleaved
        
    def demosaic(deinterleaved):
        # These are the values we already know from the input
        # x_y = the value of channel x at a site in the input of channel y
        # gb refers to green sites in the blue rows
        # gr refers to green sites in the red rows

        # Give more convenient names to the four channels we know
        r_r, g_gr, g_gb, b_b = Func('r_r'), Func('g_gr'), Func('g_gb'), Func('b_b')
        g_gr[x, y] = deinterleaved[x, y, 0]
        r_r[x, y]  = deinterleaved[x, y, 1]
        b_b[x, y]  = deinterleaved[x, y, 2]
        g_gb[x, y] = deinterleaved[x, y, 3]

        # These are the ones we need to interpolate
        b_r, g_r, b_gr, r_gr, b_gb, r_gb, r_b, g_b = Func('b_r'), Func('g_r'), Func('b_gr'), Func('r_gr'), Func('b_gb'), Func('r_gb'), Func('r_b'), Func('g_b')

        # First calculate green at the red and blue sites

        # Try interpolating vertically and horizontally. Also compute
        # differences vertically and horizontally. Use interpolation in
        # whichever direction had the smallest difference.
        gv_r  =    (g_gb[x, y-1] + g_gb[x, y])/2
        gvd_r = abs(g_gb[x, y-1] - g_gb[x, y])
        gh_r  =    (g_gr[x+1, y] + g_gr[x, y])/2
        ghd_r = abs(g_gr[x+1, y] - g_gr[x, y])
        
        g_r[x, y]  = select(ghd_r < gvd_r, gh_r, gv_r)

        gv_b  =    (g_gr[x, y+1] + g_gr[x, y])/2
        gvd_b = abs(g_gr[x, y+1] - g_gr[x, y])
        gh_b  =    (g_gb[x-1, y] + g_gb[x, y])/2
        ghd_b = abs(g_gb[x-1, y] - g_gb[x, y])

        g_b[x, y]  = select(ghd_b < gvd_b, gh_b, gv_b)

        # Next interpolate red at gr by first interpolating, then
        # correcting using the error green would have had if we had
        # interpolated it in the same way (i.e. add the second derivative
        # of the green channel at the same place).
        correction = g_gr[x, y] - (g_r[x, y] + g_r[x-1, y])/2
        r_gr[x, y] = correction + (r_r[x-1, y] + r_r[x, y])/2

        # Do the same for other reds and blues at green sites
        correction = g_gr[x, y] - (g_b[x, y] + g_b[x, y-1])/2
        b_gr[x, y] = correction + (b_b[x, y] + b_b[x, y-1])/2

        correction = g_gb[x, y] - (g_r[x, y] + g_r[x, y+1])/2
        r_gb[x, y] = correction + (r_r[x, y] + r_r[x, y+1])/2

        correction = g_gb[x, y] - (g_b[x, y] + g_b[x+1, y])/2
        b_gb[x, y] = correction + (b_b[x, y] + b_b[x+1, y])/2

        # Now interpolate diagonally to get red at blue and blue at
        # red. Hold onto your hats; this gets really fancy. We do the
        # same thing as for interpolating green where we try both
        # directions (in this case the positive and negative diagonals),
        # and use the one with the lowest absolute difference. But we
        # also use the same trick as interpolating red and blue at green
        # sites - we correct our interpolations using the second
        # derivative of green at the same sites.
        
        correction = g_b[x, y]  - (g_r[x, y] + g_r[x-1, y+1])/2
        rp_b       = correction + (r_r[x, y] + r_r[x-1, y+1])/2
        rpd_b      = abs(r_r[x, y] - r_r[x-1, y+1])

        correction = g_b[x, y]  - (g_r[x-1, y] + g_r[x, y+1])/2
        rn_b       = correction + (r_r[x-1, y] + r_r[x, y+1])/2
        rnd_b      = abs(r_r[x-1, y] - r_r[x, y+1])

        r_b[x, y]  = select(rpd_b < rnd_b, rp_b, rn_b)


        # Same thing for blue at red
        correction = g_r[x, y]  - (g_b[x, y] + g_b[x+1, y-1])/2
        bp_r       = correction + (b_b[x, y] + b_b[x+1, y-1])/2
        bpd_r      = abs(b_b[x, y] - b_b[x+1, y-1])

        correction = g_r[x, y]  - (g_b[x+1, y] + g_b[x, y-1])/2
        bn_r       = correction + (b_b[x+1, y] + b_b[x, y-1])/2
        bnd_r      = abs(b_b[x+1, y] - b_b[x, y-1])

        b_r[x, y]  =  select(bpd_r < bnd_r, bp_r, bn_r)

        # Interleave the resulting channels
        r = interleave_y(interleave_x(r_gr, r_r),
                         interleave_x(r_b, r_gb))
        g = interleave_y(interleave_x(g_gr, g_r),
                         interleave_x(g_b, g_gb))
        b = interleave_y(interleave_x(b_gr, b_r),
                         interleave_x(b_b, b_gb))


        output = Func('demosaic')
        output[x, y, c] = select(c == 0, r[x, y], 
                          select(c == 1, g[x, y], b[x, y]))


        # THE SCHEDULE
        if schedule == 0:
            # optimized for ARM
            # Compute these in chunks over tiles, vectorized by 8
            g_r.chunk(tx).vectorize(x, 8)
            g_b.chunk(tx).vectorize(x, 8)
            r_gr.chunk(tx).vectorize(x, 8)
            b_gr.chunk(tx).vectorize(x, 8)
            r_gb.chunk(tx).vectorize(x, 8)
            b_gb.chunk(tx).vectorize(x, 8)
            r_b.chunk(tx).vectorize(x, 8)
            b_r.chunk(tx).vectorize(x, 8)
            # These interleave in y, so unrolling them in y helps
            r.chunk(tx).vectorize(x, 8).unroll(y, 2)
            g.chunk(tx).vectorize(x, 8).unroll(y, 2)
            b.chunk(tx).vectorize(x, 8).unroll(y, 2)
        elif schedule == 1:
            # optimized for X86
            # Don't vectorize, because sse is bad at 16-bit interleaving
            g_r.chunk(tx)
            g_b.chunk(tx)
            r_gr.chunk(tx)
            b_gr.chunk(tx)
            r_gb.chunk(tx)
            b_gb.chunk(tx)
            r_b.chunk(tx)
            b_r.chunk(tx)
            # These interleave in x and y, so unrolling them helps
            r.chunk(tx).unroll(x, 2).unroll(y, 2)
            g.chunk(tx).unroll(x, 2).unroll(y, 2)
            b.chunk(tx).unroll(x, 2).unroll(y, 2)
        elif schedule == -1:
            # Basic naive schedule
            g_r.root()
            g_b.root()
            r_gr.root()
            b_gr.root()
            r_gb.root()
            b_gb.root()
            r_b.root()
            b_r.root()
            r.root()
            g.root()
            b.root()

        return output

    def color_correct(input, matrix_3200, matrix_7000, kelvin):
        # Get a color matrix by linearly interpolating between two
        # calibrated matrices using inverse kelvin.

        matrix = Func('matrix')
        alpha = (1.0/kelvin - 1.0/3200) / (1.0/7000 - 1.0/3200)
        val =  (matrix_3200[x, y] * alpha + matrix_7000[x, y] * (1 - alpha))
        matrix[x, y] = cast(int_t, val * 256.0) # Q8.8 fixed point
        matrix.root()

        corrected = Func('corrected')
        ir = cast(int_t, input[x, y, 0])
        ig = cast(int_t, input[x, y, 1])
        ib = cast(int_t, input[x, y, 2])

        r = matrix[3, 0] + matrix[0, 0] * ir + matrix[1, 0] * ig + matrix[2, 0] * ib
        g = matrix[3, 1] + matrix[0, 1] * ir + matrix[1, 1] * ig + matrix[2, 1] * ib
        b = matrix[3, 2] + matrix[0, 2] * ir + matrix[1, 2] * ig + matrix[2, 2] * ib

        r = cast(Int(16), r/256)
        g = cast(Int(16), g/256)
        b = cast(Int(16), b/256)
        corrected[x, y, c] = select(c == 0, r,
                             select(c == 1, g, b))

        return corrected

    def apply_curve(input, gamma, contrast):
        # copied from FCam
        curve = Func('curve')

        xf = clamp(cast(float_t, x)/1024.0, 0.0, 1.0)
        g = pow(xf, 1.0/gamma)
        b = 2.0 - pow(2.0, contrast/100.0)
        a = 2.0 - 2.0*b
        z = select(g > 0.5,
                   1.0 - (a*(1.0-g)*(1.0-g) + b*(1.0-g)),
                   a*g*g + b*g)

        val = cast(result_type, clamp(z*256.0, 0.0, 255.0))
        curve[x] = val
        curve.root() # It's a LUT, compute it once ahead of time.

        curved = Func('curved')
        curved[x, y, c] = curve[input[x, y, c]]

        return curved

    def process(raw, matrix_3200, matrix_7000, color_temp, gamma, contrast):

        processed = Func('processed')
        xi, yi = Var('xi'), Var('yi')

        denoised = hot_pixel_suppression(raw)
        deinterleaved = deinterleave(denoised)
        demosaiced = demosaic(deinterleaved)
        corrected = color_correct(demosaiced, matrix_3200, matrix_7000, color_temp)
        curved = apply_curve(corrected, gamma, contrast)

        # Schedule
        #co, ci = Var('co'), Var('ci')
        processed[tx, ty, c] = curved[tx, ty, c]
        #processed.split(c, co, ci, 3) # bound color loop to 0-3
        if schedule == 0:
            # Compute in chunks over tiles, vectorized by 8
            denoised.chunk(tx).vectorize(x, 8)
            deinterleaved.chunk(tx).vectorize(x, 8)
            corrected.chunk(tx).vectorize(x, 4)
            processed.tile(tx, ty, xi, yi, 32, 32).reorder(xi, yi, c, tx, ty)
            processed.parallel(ty)
        elif schedule == 1:
            # Same as above, but don't vectorize (sse is bad at interleaved 16-bit ops)
            denoised.chunk(tx)
            deinterleaved.chunk(tx)
            corrected.chunk(tx)
            processed.tile(tx, ty, xi, yi, 128, 128).reorder(xi, yi, c, tx, ty)
            processed.parallel(ty)
        elif schedule == -1:
            # Naive schedule
            denoised.root()
            deinterleaved.root()
            corrected.root()
            processed.root()

        return processed

    # The camera pipe is specialized on the 2592x1968 images that
    # come in, so we'll just use an image instead of a uniform image.
    #Image<int16_t> input(2592, 1968);
    input = UniformImage(UInt(16), 2, 'input')
    
    if use_uniforms:
        color_temp = Uniform(float_t, "color_temp", 3200.0)
        gamma = Uniform(float_t, "gamma", 1.8)
        contrast = Uniform(float_t, "contrast", 10.0)
    else:
        color_temp = 3700.0 #3200.0
        gamma = 2.0 #1.8
        contrast = 50.0 #10.0
        
    # shift things inwards to give us enough padding on the
    # boundaries so that we don't need to check bounds. We're going
    # to make a 2560x1920 output image, just like the FCam pipe, so
    # shift by 16, 12
    shifted = Func('shifted')
    shifted[x, y] = cast(Int(16), input[clamp(x+16, 0, input.width()-1), clamp(y+12, 0, input.height()-1)])

    if use_uniforms:
        matrix_3200 = UniformImage(float_t, 2, 'm3200')
        matrix_7000 = UniformImage(float_t, 2, 'm7000')
        matrix_3200_npy = numpy.array([[ 1.6697, -0.2693, -0.4004, -42.4346],
                                       [-0.3576,  1.0615,  1.5949, -37.1158],
                                       [-0.2175, -1.8751,  6.9640, -26.6970]],'float32')
        matrix_7000_npy = numpy.array([[ 2.2997, -0.4478,  0.1706, -39.0923],
                                       [-0.3826,  1.5906, -0.2080, -25.4311],
                                       [-0.0888, -0.7344,  2.2832, -20.0826]],'float32')
        matrix_3200.assign(matrix_3200_npy)
        matrix_7000.assign(matrix_7000_npy)
    else:
        matrix_3200 = Func('matrix_3200')
        matrix_7000 = Func('matrix_7000')
        matrix_3200[x,y] = select(y==0, select(x==0,  1.6697, select(x==1, -0.2693, select(x==2, -0.4004, -42.4346))),
                           select(y==1, select(x==0, -0.3576, select(x==1,  1.0615, select(x==2,  1.5949, -37.1158))),
                                        select(x==0, -0.2175, select(x==1, -1.8751, select(x==2,  6.9640, -26.6970)))))
        matrix_7000[x,y] = select(y==0, select(x==0,  2.2997, select(x==1, -0.4478, select(x==2,  0.1706, -39.0923))),
                           select(y==1, select(x==0, -0.3826, select(x==1,  1.5906, select(x==2, -0.2080, -25.4311))),
                                        select(x==0, -0.0888, select(x==1, -0.7344, select(x==2,  2.2832, -20.0826)))))
        matrix_3200.root()
        matrix_7000.root()

    processed = process(shifted, matrix_3200, matrix_7000, color_temp, gamma, contrast)

    # Special tuning variables interpreted by the autotuner
    tune_out_dims = OUT_DIMS
    tune_in_images = [os.path.join(inputs_dir(), '../apps/camera_pipe/raw.png')]

    if schedule == 2:
        # Autotuned schedule
        import autotune
        asched = autotune.Schedule.fromstring(processed, 'b_b.chunk(x).vectorize(x,2)\nb_gb.chunk(x).vectorize(x,8)\nb_gr.chunk(y).tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\nb_r.chunk(y).tile(x,y,_c0,_c1,8,8).vectorize(_c0,8)\ncorrected.chunk(x).vectorize(x,8)\ncurve.root().vectorize(x,4).split(x,x,_c0,16)\ncurved.root().tile(x,y,_c0,_c1,32,32).parallel(y)\n\n\ndenoised.root().tile(x,y,_c0,_c1,64,64).vectorize(_c0,8).parallel(y)\ng_b.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\ng_gb.chunk(x).vectorize(x,4)\ng_gr.chunk(y)\ng_r.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\n\n\ninterleave_x3.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\ninterleave_x4.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\ninterleave_x5.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\ninterleave_x6.root().tile(x,y,_c0,_c1,16,16).vectorize(_c0,16).parallel(y)\ninterleave_y1.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\ninterleave_y2.chunk(x).vectorize(x,8)\ninterleave_y3.chunk(x).vectorize(x,8)\nmatrix.root().tile(x,y,_c0,_c1,4,4).vectorize(_c0,4).parallel(y)\nmatrix_3200.root().tile(x,y,_c0,_c1,4,4).parallel(y)\n\nprocessed.root().vectorize(tx,8)\nr_b.chunk(y).vectorize(x,8)\nr_gb.chunk(y).vectorize(x,8)\nr_gr.chunk(x)\nr_r.chunk(y)\nshifted.chunk(x).vectorize(x,4)')
        print asched
        asched.apply()
    
    tune_ref_schedules = {'human': """
            g_r.chunk(tx).vectorize(x, 8)
            g_b.chunk(tx).vectorize(x, 8)
            r_gr.chunk(tx).vectorize(x, 8)
            b_gr.chunk(tx).vectorize(x, 8)
            r_gb.chunk(tx).vectorize(x, 8)
            b_gb.chunk(tx).vectorize(x, 8)
            r_b.chunk(tx).vectorize(x, 8)
            b_r.chunk(tx).vectorize(x, 8)
            r.chunk(tx).vectorize(x, 8).unroll(y, 2)
            g.chunk(tx).vectorize(x, 8).unroll(y, 2)
            b.chunk(tx).vectorize(x, 8).unroll(y, 2)
            matrix_3200.root()
            matrix_7000.root()
            denoised.chunk(tx).vectorize(x, 8)
            deinterleaved.chunk(tx).vectorize(x, 8)
            corrected.chunk(tx).vectorize(x, 4)
            processed.tile(tx, ty, xi, yi, 32, 32).reorder(xi, yi, c, tx, ty)
            processed.parallel(ty)
            """}
    
    #def evaluate(in_png):
    #    output = Image(UInt(8), 2560, 1920, 3); # image size is hard-coded for the N900 raw pipeline
    #autotune.print_tunables(processed)
    #import autotune
    #g_r = all_funcs(processed)['g_r']
    #print 'caller_vars for g_r:', autotune.caller_vars(processed, g_r)
    #root_all(processed)
    
    # In C++-11, this can be done as a simple initializer_list {color_temp,gamma,etc.} in place.
    #Func::Arg args[] = {color_temp, gamma, contrast, input, matrix_3200, matrix_7000};
    #processed.compileToFile("curved", std::vector<Func::Arg>(args, args+6));
    return (input, processed, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, local_d['in_image'], disp_time=True, out_dims=OUT_DIMS)().show()
    #input.assign(Image(UInt(16), '../../apps/camera_pipe/raw.png'))
    #output = Image(UInt(8), 2560, 1920, 3)
    #out_func.realize(output)
    #output.save('out.png')
    
if __name__ == '__main__':
    main()
    