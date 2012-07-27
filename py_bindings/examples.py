
from halide import *

def blur(dtype=UInt(16), counter=[0]):
    s = '_blur%d'%counter[0]
    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    input_clamped = Func('input_clamped'+s)
    blur_x = Func('blur_x'+s)
    blur_y = Func('blur_y'+s)

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
    blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4
    counter[0] += 1
    return (input, blur_y)

def dilate(dtype=UInt(16), counter=[0]):
    s = '_dilate%d'%counter[0]
    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    input_clamped = Func('input_clamped'+s)
    dilate = Func('dilate'+s)

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    subexp = input_clamped[x,y]
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                subexp = max(subexp, input_clamped[x+dx,y+dy])
    dilate[x,y,c] = subexp #min(min(input_clamped[x-1,y-1,c],input_clamped[x,y-1,c]),input_clamped[x+1,y-1,c])
    counter[0] += 1
    return (input, dilate)

def local_laplacian(dtype=UInt(16), counter=[0]):
    J = 8
    
    s = '_laplacian%d'%counter[0]
    float_t = Float(32)
    int_t = Int(32)

    def downsample(f):
        downx, downy = Func(), Func()
        
        downx[x,y] = (f[2*x-1, y] + 3.0*(f[2*x,y]+f[2*x+1,y]) + f[2*x+2,y])/8.0
        downy[x,y] = (downx[x,2*y-1] + 3.0*(downx[x,2*y]+downx[x,2*y+1]) + downx[x,2*y+2])/8.0

        return downy
    
    def upsample(f):
        upx, upy = Func(), Func()
        
        upx[x,y] = 0.25 * f[(x/2)-1+2*(x%2),y] + 0.75 * f[x/2,y]
        upy[x,y] = 0.25 * upx[x, (y/2) - 1 + 2*(y%2)] + 0.75 * upx[x,y/2]
        
        return upy
        
    levels = Uniform(int_t, 'levels'+s, 8)
    alpha = Uniform(float_t, 'alpha'+s, 1.0)
    beta = Uniform(float_t, 'beta'+s, 1.0)
    input = UniformImage(dtype, 3, 'input'+s)
    
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    k = Var('k'+s)
    
    fx = cast(float_t, x/256.0)
    remap = Func('remap'+s)
    -fx
    -fx*fx
    -fx*fx/2.0
    exp(-fx*fx/2.0)
    fx*exp(-fx*fx/2.0)
    alpha*fx*exp(-fx*fx/2.0)
    remap[x] = alpha*fx*exp(-fx*fx/2.0)
    
    floating = Func('floating'+s)
    floating[x,y,c] = cast(float_t, input[x,y,c]/float(dtype.maxval()))
    
    clamped = Func('clamped'+s)
    clamped[x,y,c] = floating[clamp(x,cast(int_t,0),cast(int_t,input.width()-1)),
                              clamp(y,cast(int_t,0),cast(int_t,input.height()-1)), c]
    gray = Func('gray'+s)
    gray[x,y] = 0.299*clamped[x,y,0]+0.587*clamped[x,y,1]+0.114*clamped[x,y,2]
    
    gPyramid = [Func('gPyramid%d'%i+s) for i in range(J)]
    idx = gray[x,y]*cast(float_t, levels-1)*256.0
    idx = clamp(cast(int_t, idx), cast(int_t, 0), cast(int_t, (levels-1)*256))
    gPyramid[0][x,y,k] = beta*gray[x,y] + remap[idx-256*k]
    for j in range(1,J):
        gPyramid[j][x,y,k] = downsample(gPyramid[k-1])[x,y,k]
        
    lPyramid = [Func('lPyramid%d'%i+s) for i in range(J)]
    lPyramid[J-1] = gPyramid[J-1]
    for j in range(J-2)[::-1]:
        lPyramid[j][x,y,k] = gPyramid[j][x,y,k] - upsample(gPyramid[j+1])[x,y,k]
    
    inGPyramid = [Func('inGPyramid%d'%i+s) for i in range(J)]
    inGPyramid[0] = gray
    for j in range(1,J):
        inGPyramid[j][x,y] = downsample(inGPyramid[j-1])[x,y]
    
    outLPyramid = [Func('outLPyramid%d'%i+s) for i in range(J)]
    for j in range(J):
        level = inGPyramid[j][x,y]*cast(float_t, levels-1)
        li = clamp(cast(int_t, level), cast(int_t, 0), cast(int_t, levels-2))
        lf = level - cast(float_t, li)
        outLPyramid[j][x,y] = (1.0-lf)*lPyramid[j][x,y,li] + lf*lPyramid[j][x,y,li+1]
    
    outGPyramid = [Func('outGPyramid%d'%i+s) for i in range(J)]
    outGPyramid[J-1] = outLPyramid[J-1]
    for j in range(J-2)[::-1]:
        outGPyramid[j][x,y] = upsample(outGPyramid[j+1])[x,y] + outLPyramid[j][x,y]
    
    color = Func('color'+s)
    color[x,y,c] = outGPyramid[0][x,y] * clamped[x,y,c] / gray[x,y]
    
    output = Func('output'+s)
    output[x,y,c] = cast(dtype, clamp(color[x,y,c], 0.0, 1.0)*(dtype.maxval()))
    
    for f in halide.all_funcs(output).values():
        f.root()
        
    return output
    