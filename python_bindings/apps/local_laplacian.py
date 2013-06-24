
"""
Local Laplacian, see e.g. Aubry et al 2011, "Fast and Robust Pyramid-based Image Processing".
"""

import sys
from halide import *

int_t = Int(32)
float_t = Float(32)

def main(J=8, dtype=UInt(16)):
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
    
    levels = Param(int_t, 'levels', 8)
    alpha = Param(float_t, 'alpha', 1.0) #1.0)
    beta = Param(float_t, 'beta', 1.0)

    input = ImageParam(dtype, 3, 'input')
    
    x = Var('x')
    y = Var('y')
    c = Var('c')
    k = Var('k')
    
    fx = cast(float_t, x/256.0)
    remap = Func('remap')
    remap[x] = (alpha/cast(float_t, levels-1))*fx*exp(-fx*fx/2.0)
    
    floating = Func('floating')
    floating[x,y,c] = cast(float_t, input[x,y,c])/float(dtype.typical_max())
    
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
    color[x,y,c] = outGPyramid[0][x,y] * (clamped[x,y,c]+0.01) / (gray[x,y]+0.01)
    
    output = Func('output')
    output[x,y,c] = cast(dtype, clamp(color[x,y,c], cast(float_t,0.0), cast(float_t,1.0))*float(dtype.typical_max()))
    
    yi = Var('yi')
    
    # CPU schedule
    output.compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4)
    remap.compute_root()
    gray.compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4)
    for j in range(4):
        if j > 0: inGPyramid[j].compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4)
        if j > 0: gPyramid[j].compute_root().parallel(k).vectorize(x, 4)
        outGPyramid[j].compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4)
    for j in range(4,J):
        inGPyramid[j].compute_root().parallel(y)
        gPyramid[j].compute_root().parallel(k)
        outGPyramid[j].compute_root().parallel(y)
    
    I = filter_image(input, output, builtin_image('rgb.png'), disp_time=True)()
    if len(sys.argv) >= 2:
        I.save(sys.argv[1])
    else:
        I.show()

if __name__ == '__main__':
    main()
