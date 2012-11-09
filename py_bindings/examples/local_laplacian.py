import sys; sys.path += ['..', '.']
from halide import *
import autotune

int_t = Int(32)
float_t = Float(32)

def filter_func(J=8, dtype=UInt(16), use_uniforms=False):
    "Local Laplacian."

    downsample_counter=[0] 
    upsample_counter=[0]
    
    def downsample(f):
        downx, downy = Func('downx%d'%downsample_counter[0]), Func('downy%d'%downsample_counter[0])
        downsample_counter[0] += 1
        
        downx[x,y] = (f[2*x-1, y] + 3.0*(f[2*x,y]+f[2*x+1,y]) + f[2*x+2,y])/8.0
        downy[x,y] = (downx[x,2*y-1] + 3.0*(downx[x,2*y]+downx[x,2*y+1]) + downx[x,2*y+2])/8.0

        return downy
    
    def upsample(f):
        upx, upy = Func('upx%d'%upsample_counter[0]), Func('upy%d'%upsample_counter[0])
        upsample_counter[0] += 1
        
        upx[x,y] = 0.25 * f[(x/2)-1+2*(x%2),y] + 0.75 * f[x/2,y]
        upy[x,y] = 0.25 * upx[x, (y/2) - 1 + 2*(y%2)] + 0.75 * upx[x,y/2]
        
        return upy
    
    if use_uniforms:
        levels = Uniform(int_t, 'levels', 8)
        alpha = Uniform(float_t, 'alpha', 1.0) #1.0)
        beta = Uniform(float_t, 'beta', 1.0)
    else:
        levels = 8
        alpha = 1.0
        beta = 1.0
    input = UniformImage(dtype, 3, 'input')
    
    x = Var('x')
    y = Var('y')
    c = Var('c')
    k = Var('k')
    
    fx = cast(float_t, x/256.0)
    remap = Func('remap')
    remap[x] = (alpha/cast(float_t, levels-1))*fx*exp(-fx*fx/2.0)
    
    floating = Func('floating')
    floating[x,y,c] = cast(float_t, input[x,y,c])/float(dtype.maxval())
    
    clamped = Func('clamped')
    clamped[x,y,c] = floating[clamp(x,cast(int_t,0),cast(int_t,input.width()-1)),
                              clamp(y,cast(int_t,0),cast(int_t,input.height()-1)), c]
    gray = Func('gray')
    gray[x,y] = 0.299*clamped[x,y,0]+0.587*clamped[x,y,1]+0.114*clamped[x,y,2]
    
    gPyramid = [Func('gPyramid%d'%i) for i in range(J)]
    idx = gray[x,y]*cast(float_t, levels-1)*256.0
    idx = clamp(cast(int_t, idx), cast(int_t, 0), cast(int_t, (levels-1)*256))
    gPyramid[0][x,y,k] = beta*gray[x,y] + remap[idx-256*k]
    for j in range(1,J):
        gPyramid[j][x,y,k] = downsample(gPyramid[j-1])[x,y,k]

    lPyramid = [Func('lPyramid%d'%i) for i in range(J)]
    lPyramid[J-1] = gPyramid[J-1]
    for j in range(J-1)[::-1]:
        lPyramid[j][x,y,k] = gPyramid[j][x,y,k] - upsample(gPyramid[j+1])[x,y,k]
    
    inGPyramid = [Func('inGPyramid%d'%i) for i in range(J)]
    inGPyramid[0] = gray
    for j in range(1,J):
        inGPyramid[j][x,y] = downsample(inGPyramid[j-1])[x,y]
    
    outLPyramid = [Func('outLPyramid%d'%i) for i in range(J)]
    for j in range(J):
        level = inGPyramid[j][x,y]*cast(float_t, levels-1)
        li = clamp(cast(int_t, level), cast(int_t, 0), cast(int_t, levels-2))
        lf = level - cast(float_t, li)
        outLPyramid[j][x,y] = (1.0-lf)*lPyramid[j][x,y,li] + lf*lPyramid[j][x,y,li+1]
    
    outGPyramid = [Func('outGPyramid%d'%i) for i in range(J)]
    outGPyramid[J-1] = outLPyramid[J-1]
    for j in range(J-1)[::-1]:
        outGPyramid[j][x,y] = upsample(outGPyramid[j+1])[x,y] + outLPyramid[j][x,y]
    
    color = Func('color')
    #color[x,y,c] = outGPyramid[0][x,y] * clamped[x,y,c] / gray[x,y]
    color[x,y,c] = outGPyramid[0][x,y] * (clamped[x,y,c]+0.01) / (gray[x,y]+0.01)
    
    output = Func('output')
    output[x,y,c] = cast(dtype, clamp(color[x,y,c], cast(float_t,0.0), cast(float_t,1.0))*float(dtype.maxval()))
    
    root_all(output)
    #import autotune
    #print autotune.root_all_str(output)
    #autotune.print_root_all(output)
    
    human_schedule = 'remap.root()\noutput.root().split(y, y, _c0, 32).parallel(y).vectorize(x, 4)\n'
    for j in range(J):
        human_schedule += '%s.root().split(y, y, _c0, 4).parallel(y).vectorize(x, 4)\n'%inGPyramid[j].name()
        if j > 0:
            human_schedule += 'gPyramid%d.root().parallel(k).vectorize(x, 4)\n'%j
        human_schedule += '%s.root().split(y, y, _c0, 4).parallel(y).vectorize(x, 4)\n'%outGPyramid[j].name()
    
    if is_cuda():
        human_schedule = 'remap.root()\n'
        human_schedule += 'output.root().cudaTile(x, y, 32, 32)\n'
        for j in range(J):
            blockw = blockh = 32
            if j > 3:
                blockw = blockh = 2
            human_schedule += 'inGPyramid%d.root().cudaTile(x, y, %d, %d)\n'%(j, blockw, blockh)
            human_schedule += 'gPyramid%d.root().cudaTile(x, y, %d, %d)\n'%(j, blockw, blockh)
            human_schedule += 'outGPyramid%d.root().cudaTile(x, y, %d, %d)\n'%(j, blockw, blockh)

    # Special variables interpreted by autotuner
    tune_ref_schedules = {'human': human_schedule}
    tune_constraints = autotune.bound_recursive(output, 'c', 0, 3)

    #print '# schedules:'
    #import math
    #print math.log(autotune.lower_bound_schedules(output),10)
    #sys.exit(1)
    
    return (input, output, None, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo3.png'), disp_time=True)().show()
    #filter_image(input, out_func, os.path.join(inputs_dir(), 'lena_crop.png'), disp_time=True)().show()
    
    # Watch process memory usage for JIT in your system monitor
    #while 1:
    #    filter_image(input, out_func, os.path.join(inputs_dir(), 'apollo3.png'), disp_time=True)()

if __name__ == '__main__':
    main()

    
