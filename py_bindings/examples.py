
from halide import *
import math

int_t = Int(32)
float_t = Float(32)

ROOT_ALL = True

def blur(dtype=UInt(16), counter=[0]):
    "Simple 3x3 blur."
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
    if ROOT_ALL:
        root_all(blur_y)
        input_clamped.reset()
    return (input, blur_y, None, locals())

def dilate(dtype=UInt(16), counter=[0]):
    "Dilate on 3x3 stencil."
    s = '_dilate%d'%counter[0]
    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    input_clamped = Func('input_clamped'+s)
    dilate = Func('dilate'+s)

    input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    subexp = input_clamped[x,y,c]  # TODO Debug buggy
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                subexp = max(subexp, input_clamped[x+dx,y+dy,c])
    dilate[x,y,c] = subexp #min(min(input_clamped[x-1,y-1,c],input_clamped[x,y-1,c]),input_clamped[x+1,y-1,c])
    counter[0] += 1
    if ROOT_ALL:
        root_all(dilate)
        input_clamped.reset()
    return (input, dilate, None, locals())

def local_laplacian(dtype=UInt(16), counter=[0]):
    "Local Laplacian."
    print 'local_laplacian', counter[0], dtype.maxval()
    import halide
    J = 8
    
    s = '_laplacian%d'%counter[0]
    downsample_counter=[0] 
    upsample_counter=[0]
    
    def downsample(f):
        downx, downy = Func('downx%d'%downsample_counter[0]+s), Func('downy%d'%downsample_counter[0]+s)
        downsample_counter[0] += 1
        
        downx[x,y] = (f[2*x-1, y] + 3.0*(f[2*x,y]+f[2*x+1,y]) + f[2*x+2,y])/8.0
        downy[x,y] = (downx[x,2*y-1] + 3.0*(downx[x,2*y]+downx[x,2*y+1]) + downx[x,2*y+2])/8.0

        return downy
    
    def upsample(f):
        upx, upy = Func('upx%d'%upsample_counter[0]+s), Func('upy%d'%upsample_counter[0]+s)
        upsample_counter[0] += 1
        
        upx[x,y] = 0.25 * f[(x/2)-1+2*(x%2),y] + 0.75 * f[x/2,y]
        upy[x,y] = 0.25 * upx[x, (y/2) - 1 + 2*(y%2)] + 0.75 * upx[x,y/2]
        
        return upy
        
    levels = Uniform(int_t, 'levels'+s, 8)
    alpha = Uniform(float_t, 'alpha'+s, 1.0) #1.0)
    beta = Uniform(float_t, 'beta'+s, 1.0)
    input = UniformImage(dtype, 3, 'input'+s)
    
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    k = Var('k'+s)
    
    fx = cast(float_t, x/256.0)
    remap = Func('remap'+s)
    remap[x] = (alpha/cast(float_t, levels-1))*fx*exp(-fx*fx/2.0)
    
    floating = Func('floating'+s)
    floating[x,y,c] = cast(float_t, input[x,y,c])/float(dtype.maxval())
    
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
        gPyramid[j][x,y,k] = downsample(gPyramid[j-1])[x,y,k]

    lPyramid = [Func('lPyramid%d'%i+s) for i in range(J)]
    lPyramid[J-1] = gPyramid[J-1]
    for j in range(J-1)[::-1]:
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
    for j in range(J-1)[::-1]:
        outGPyramid[j][x,y] = upsample(outGPyramid[j+1])[x,y] + outLPyramid[j][x,y]
    
    color = Func('color'+s)
    color[x,y,c] = outGPyramid[0][x,y] * clamped[x,y,c] / gray[x,y]
    
    output = Func('output'+s)
    output[x,y,c] = cast(dtype, clamp(color[x,y,c], cast(float_t,0.0), cast(float_t,1.0))*float(dtype.maxval()))
    
    root_all(output)
    #print 'Done with local_laplacian', counter[0]
    counter[0] += 1

    if ROOT_ALL:
        root_all(output)

    return (input, output, None, locals())

def boxblur_mode(dtype, counter, is_sat):
    print 'box', is_sat, counter[0]
    s = '_box%d_%d'%(is_sat, counter[0])
    input = UniformImage(dtype, 3, 'input'+s)
    box_size = Uniform(int_t, 'box_size'+s, 15)

    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    
    sum = Func('sum'+s)                     # Cumulative sum in x and y or summed area table
    zero = cast(int_t,0)
    w1 = cast(int_t,input.width()-1)
    h1 = cast(int_t,input.height()-1)
    if is_sat:
        r = RDom(0,input.width(),0,input.height(),'r'+s)
        rx = r.x
        ry = r.y
        sum[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        #sum[rx,ry,c] = sum[rx,ry,c] + sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
        sum[rx,ry,c] += sum[max(rx-1,zero),ry,c] + sum[rx,max(ry-1,zero),c] - sum[max(rx-1,zero),max(ry-1,zero),c]
    else:
        rx = RDom(0,input.width(),'rx'+s)
        ry = RDom(0,input.height(),'ry'+s)
        sumx = Func('sumx'+s)
        sumx[x,y,c] = cast(float_t if dtype.isFloat() else int_t, input[x,y,c])
        sumx[rx,y,c] += sumx[max(rx-1,zero),y,c]
        sum[x,y,c] = sumx[x,y,c]
        sum[x,ry,c] += sum[x,max(ry-1,zero),c]
    sum_clamped = Func('sum_clamped'+s)
    sum_clamped[x,y,c] = sum[clamp(x,zero,w1),clamp(y,zero,h1),c]
    
    weight = Func('weight'+s)
    weight[x,y] = ((min(x+box_size,w1)-max(x-box_size-1,zero))*
                   (min(y+box_size,h1)-max(y-box_size-1,zero)))
    
    output = Func('output'+s)
    output[x,y,c] = cast(dtype,
                    (sum_clamped[x+box_size  ,y+box_size  ,c]-
                     sum_clamped[x-box_size-1,y+box_size  ,c]-
                     sum_clamped[x+box_size  ,y-box_size-1,c]+
                     sum_clamped[x-box_size-1,y-box_size-1,c])/weight[x,y])
    
    if ROOT_ALL:
        root_all(output)

    counter[0] += 1
    return (input, output, None, locals())

def boxblur_sat(dtype=UInt(16), counter=[0]):
    "Box blur (implemented with summed area table)."
    return boxblur_mode(dtype, counter, True)
    
def boxblur_cumsum(dtype=UInt(16), counter=[0]):
    "Box blur (implemented with cumsum)."
    return boxblur_mode(dtype, counter, False)

def snake(dtype=UInt(8), counter=[0], in_filename='blood_cells_small.png'):
    "Snake segmentation."
    exit_on_signal()
    s = '_snake%d'%counter[0]
    s0 = s
    counter[0] += 1
    x = Var('x'+s)
    y = Var('y'+s)
    dx_counter=[0]
    dy_counter=[0]
    lap_counter=[0]
    distReg_counter=[0]
    Dirac_counter=[0]
    
    def dx(f):
        out = Func('dx%d'%dx_counter[0]+s)
        dx_counter[0] += 1
        out[x,y] = 0.5 * (f[x+1,y] - f[x-1,y])
        return out

    def dy(f):
        out = Func('dy%d'%dy_counter[0]+s)
        dy_counter[0] += 1
        out[x,y] = 0.5 * (f[x,y+1] - f[x,y-1])
        return out

    def lap(f):
        out = Func('lap%d'%lap_counter[0]+s)
        lap_counter[0] += 1
        out[x,y] = f[x+1,y] + f[x-1,y] + f[x,y+1] + f[x,y-1] - 4.0 * f[x,y]
        return out

    def distReg_p2(phi):
        phi_x = dx(phi)
        phi_y = dy(phi)
        s = sqrt(phi_x * phi_x + phi_y * phi_y)

        ps = select(s <= 1.0,
                    sin(2.0 * math.pi * s / (2.0 * math.pi)),
                    s - 1.0)
        n = select(ps == 0.0, 1.0, ps)
        d = select(s == 0.0, 1.0, s)

        proxy_x = Func('proxy_x%d'%distReg_counter[0]+s0)
        proxy_x.assign((n / d) * phi_x - phi_x)
  
        proxy_y = Func('proxy_y%d'%distReg_counter[0]+s0)
        proxy_y.assign((n / d) * phi_y - phi_y)

        out = Func('distReg%d'%distReg_counter[0]+s0)
        out.assign(dx(proxy_x) + dy(proxy_y) + lap(phi))
        distReg_counter[0] += 1
        
        return out

    def Dirac(input, sigma):
        out = Func('Dirac%d'%Dirac_counter[0]+s)
        Dirac_counter[0] += 1
        out[x,y] = select((input[x,y] <= sigma) & (input[x,y] >= -sigma),
                    1.0 / (2.0 * sigma) * (1.0 + cos(math.pi * input[x,y] / sigma)),
                    0.0)
        return out

    def drlse_edge(phi_0, g, lambd, mu, alpha, epsilon, timestep, iter):
        phi = [Func('phi%d'%i+s0) for i in range(iter+1)]
        phi[0][x,y] = phi_0[x,y]
  
        vx = dx(g)[x,y]
        vy = dy(g)[x,y]
        
        for k in range(iter):
            phi_x = dx(phi[k])[x,y]
            phi_y = dy(phi[k])[x,y]

            s = sqrt(phi_x * phi_x + phi_y * phi_y)
    
            smallNumber = 1e-10

            Nx,Ny = Func('Nx%d'%k+s0), Func('Ny%d'%k+s0)
            Nx[x,y] = phi_x / (s + smallNumber)
            Ny[x,y] = phi_y / (s + smallNumber)
       
            ddx = dx(Nx)[x,y]
            ddy = dy(Ny)[x,y]
            curvature = ddx + ddy
            distRegTerm = distReg_p2(phi[k])[x,y]
            diracPhi = Dirac(phi[k],epsilon)[x,y]
            areaTerm = diracPhi * g[x,y]
            edgeTerm = diracPhi * ((vx * Nx[x,y] + vy * Ny[x,y]) + g[x,y] * curvature)

            phi[k+1][x,y] = phi[k][x,y] + timestep * (mu * distRegTerm + lambd * edgeTerm + alpha * areaTerm)

        return phi[-1]

    def blur(image, sigma):
        gaussian = Func('gaussian'+s)
        gaussian[x] = exp(-(x/sigma)*(x/sigma)*0.5)
        
        # truncate to 3 sigma and normalize
        radius = int(3*sigma + 1.0)
        i = RDom(-radius, 2*radius+1,'i'+s)
#        i = i.x
        normalized = Func('normalized'+s)
        normalized[x] = gaussian[x] / sum(gaussian[i]) # Uses an inline reduction

        # Convolve the input using two reductions
        blurx, blury = Func('blurx'+s), Func('blury'+s)
#        print image[x+i, y] * normalized[i]
#        print 'a', im.width(), im.height()
#        print blurx[x,y]
        blurx[x, y] = 0.0
        blurx[x, y] += image[x+i, y] * normalized[i]
#        print 'b'
        blury[x, y] = 0.0
        blury[x, y] += blurx[x, y+i] * normalized[i]

        # Schedule the lot as root 
        image.root()
        gaussian.root()
        normalized.root()
        blurx.root()
        blury.root()

        return blury

    timestep = 5.0
    mu = 0.2 / timestep
    iter_inner = 1
    iter_outer = 450 #1000
    lambd = 6.0
    alpha = 1.5
    epsilon = 1.5
    sigma = 1.5
    padding = 5
    background = 255.0 * 0.98
    selectPadding = 10

    im = Image(UInt(8), in_filename)
  
    gray = Func('gray'+s)
    gray[x, y] = max(cast(float_t, im[x, y, 0]), max(cast(float_t, im[x, y, 1]), cast(float_t, im[x, y, 2])))

    clamped = Func('clamped'+s)
    clamped[x, y] = gray[clamp(x, cast(int_t, 0), cast(int_t, im.width())),
                         clamp(y, cast(int_t, 0), cast(int_t, im.height()))]
    
    blurred_input = Func('blurred_input'+s)
    blurred_input.assign(blur(clamped,sigma))

    input_dx, input_dy = Func('input_dx'+s), Func('input_dy'+s)
    input_dx.assign(dx(blurred_input))
    input_dy.assign(dy(blurred_input))

    g_proxy = Func('g_proxy'+s)
    g_proxy.assign(1.0 / (1.0 + input_dx * input_dx + input_dy * input_dy))
  
    g_buf = g_proxy.realize(im.width(), im.height())

    phi_init = Func('phi_init'+s)
    phi_init[x,y] = select((x >= selectPadding)
                            & (x < im.width() - selectPadding)
                            & (y >= selectPadding)
                            & (y < im.height() - selectPadding),
                            -2.0, 2.0)
    phi_input = UniformImage(Float(32), 2)

    phi_clamped = Func('phi_clamped'+s)
    phi_clamped[x,y] = phi_input[clamp(x,cast(int_t,0),cast(int_t,im.width()-1)),
                                 clamp(y,cast(int_t,0),cast(int_t,im.height()-1))]
  
    g_clamped = Func('g_clamped'+s)
    g_clamped[x, y] = g_buf[clamp(x, cast(int_t,0), cast(int_t,g_buf.width()-1)),
                            clamp(y, cast(int_t,0), cast(int_t,g_buf.height()-1))]

    phi_new = Func('phi_new'+s)
    phi_new.assign(drlse_edge(phi_clamped,g_clamped,lambd,mu,alpha,epsilon,timestep,iter_inner))

    def evaluate(in_png):
        T0 = time.time()
        phi_buf = phi_init.realize(im.width(), im.height())
        phi_buf2 = Image(float_t, im.width(), im.height())
        for n in range(iter_outer):
            if n%10 == 9:
                print 'Iteration %d / %d. Average time per iteration = %f ms'%(n+1, iter_outer, time.time()-T0)
            phi_input.assign(phi_buf)
            phi_new.realize(phi_buf2)
            phi_buf, phi_buf2 = phi_buf2, phi_buf
    
        masked = Func('masked'+s)
        c = Var('c'+s)
        masked[x,y,c] = select(phi_buf[x, y] < 0.0, im[x, y, c], im[x, y, c]/4)
        out = masked.realize(im.width(), im.height(), 3)
        return out
    #Image(UInt(8),out).save('snake_out.png')

    if ROOT_ALL:
        root_all(phi_new)

    return (im, phi_new, evaluate, locals())

def interpolate(dtype=UInt(16), counter=[0]):
    "Fast interpolation using a pyramid."
    import halide
    s = '_interpolate%d'%counter[0]
    counter[0] += 1

    input = UniformImage(dtype, 3, 'input'+s)
    x = Var('x'+s)
    y = Var('y'+s)
    c = Var('c'+s)
    levels = 10
    
    downsampled = [Func('downsampled%d'%i+s) for i in range(levels)]
    interpolated = [Func('interpolated%d'%i+s) for i in range(levels)]
    level_widths = [Uniform(int_t,'level_widths%d'%i+s) for i in range(levels)]
    level_heights = [Uniform(int_t,'level_heights%d'%i+s) for i in range(levels)]

    downsampled[0][x,y] = (input[x,y,0] * input[x,y,3],
                           input[x,y,1] * input[x,y,3],
                           input[x,y,2] * input[x,y,3],
                           input[x,y,3])
    
    for l in range(1, levels):
        clamped = Func('clamped%d'%l+s)
        clamped[x,y,c] = downsampled[l-1][clamp(cast(int_t,x),cast(int_t,0),cast(int_t,level_widths[l-1]-1)),
                                          clamp(cast(int_t,y),cast(int_t,0),cast(int_t,level_heights[l-1]-1)), c]
        downx = Func('downx%d'%l+s)
        downx[x,y,c] = (clamped[x*2-1,y,c] + 2.0 * clamped[x*2,y,c] + clamped[x*2+1,y,c]) / 4.0
        downsampled[l][x,y,c] = (downx[x,y*2-1,c] + 2.0 * downx[x,y*2,c] + downx[x,y*2+1,c]) / 4.0
        
    interpolated[levels-1][x,y,c] = downsampled[levels-1][x,y,c]
    for l in range(levels-1)[::-1]:
        upsampledx, upsampled = Func('upsampledx%d'%l+s), Func('upsampled%d'%l+s)
        upsampledx[x,y,c] = 0.5 * (interpolated[l+1][x/2 + (x%2),y,c] + interpolated[l+1][x/2,y,c])
        upsampled[x,y,c] = 0.5 * (upsampledx[x, y/2 + (y%2),c] + upsampledx[x,y/2,c])
        interpolated[l][x,y,c] = downsampled[l][x,y,c] + (1.0 - downsampled[l][x,y,3]) * upsampled[x,y,c]

    final = Func('final'+s)
    final[x,y] = (interpolated[0][x,y,0] / interpolated[0][x,y,3],
                  interpolated[0][x,y,1] / interpolated[0][x,y,3],
                  interpolated[0][x,y,2] / interpolated[0][x,y,3],
                  1.0)
    root_all(final)
    
    #print 'interpolate: finished function setup'
    
    def evaluate(in_png):
        #print 'interpolate evaluate'
        width  = in_png.width()
        height = in_png.height()
        print width, height
        for l in range(levels):
            level_widths[l].assign(width)
            level_heights[l].assign(height)
            width = width/2 + 1
            height = height/2 + 1
        print in_png.width(), in_png.height(), 'realizing'
        out = final.realize(in_png.width(), in_png.height(), 4)
        #print 'evaluate realized, returning'
        return out
    
    #print 'interpolate: returning'

    if ROOT_ALL:
        root_all(final)

    return (input, final, evaluate, locals())
