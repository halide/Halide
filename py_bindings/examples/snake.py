import sys; sys.path += ['..', '.']
import math
from halide import *

DEFAULT_FILENAME = os.path.join(inputs_dir(), 'blood_cells_small_crop.png')

int_t = Int(32)
float_t = Float(32)

def filter_func(dtype=UInt(8), in_filename=DEFAULT_FILENAME):
    "Snake segmentation."
    exit_on_signal()

    x = Var('x')
    y = Var('y')
    dx_counter=[0]
    dy_counter=[0]
    lap_counter=[0]
    distReg_counter=[0]
    Dirac_counter=[0]
    
    def dx(f):
        out = Func('dx%d'%dx_counter[0])
        dx_counter[0] += 1
        out[x,y] = 0.5 * (f[x+1,y] - f[x-1,y])
        return out

    def dy(f):
        out = Func('dy%d'%dy_counter[0])
        dy_counter[0] += 1
        out[x,y] = 0.5 * (f[x,y+1] - f[x,y-1])
        return out

    def lap(f):
        out = Func('lap%d'%lap_counter[0])
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

        proxy_x = Func('proxy_x%d'%distReg_counter[0])
        proxy_x.assign((n / d) * phi_x - phi_x)
  
        proxy_y = Func('proxy_y%d'%distReg_counter[0])
        proxy_y.assign((n / d) * phi_y - phi_y)

        out = Func('distReg%d'%distReg_counter[0])
        out.assign(dx(proxy_x) + dy(proxy_y) + lap(phi))
        distReg_counter[0] += 1
        
        return out

    def Dirac(input, sigma):
        out = Func('Dirac%d'%Dirac_counter[0])
        Dirac_counter[0] += 1
        out[x,y] = select((input[x,y] <= sigma) & (input[x,y] >= -sigma),
                    1.0 / (2.0 * sigma) * (1.0 + cos(math.pi * input[x,y] / sigma)),
                    0.0)
        return out

    def drlse_edge(phi_0, g, lambd, mu, alpha, epsilon, timestep, iter):
        phi = [Func('phi%d'%i) for i in range(iter+1)]
        phi[0][x,y] = phi_0[x,y]
  
        vx = dx(g)[x,y]
        vy = dy(g)[x,y]
        
        for k in range(iter):
            phi_x = dx(phi[k])[x,y]
            phi_y = dy(phi[k])[x,y]

            s = sqrt(phi_x * phi_x + phi_y * phi_y)
    
            smallNumber = 1e-10

            Nx,Ny = Func('Nx%d'%k), Func('Ny%d'%k)
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
        gaussian = Func('gaussian')
        gaussian[x] = exp(-(x/sigma)*(x/sigma)*0.5)
        
        # truncate to 3 sigma and normalize
        radius = int(3*sigma + 1.0)
        i = RDom(-radius, 2*radius+1,'i')
#        i = i.x
        normalized = Func('normalized')
        normalized[x] = gaussian[x] / sum(gaussian[i]) # Uses an inline reduction

        # Convolve the input using two reductions
        blurx, blury = Func('blurx'), Func('blury')
        blurx[x, y] = 0.0
        blurx[x, y] += image[x+i, y] * normalized[i]
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

    #im = Image(UInt(8), in_filename)
    im0 = UniformImage(UInt(8), 3)
    im = Image(UInt(8), in_filename)
    im0.assign(im)
    
    gray = Func('gray')
    gray[x, y] = max(cast(float_t, im0[x, y, 0]), max(cast(float_t, im0[x, y, 1]), cast(float_t, im0[x, y, 2])))

    clamped = Func('clamped')
    clamped[x, y] = gray[clamp(x, cast(int_t, 0), cast(int_t, im.width()-1)),
                         clamp(y, cast(int_t, 0), cast(int_t, im.height()-1))]
    
    blurred_input = Func('blurred_input')
    blurred_input.assign(blur(clamped,sigma))

    input_dx, input_dy = Func('input_dx'), Func('input_dy')
    input_dx.assign(dx(blurred_input))
    input_dy.assign(dy(blurred_input))

    g_proxy = Func('g_proxy')
    g_proxy.assign(1.0 / (1.0 + input_dx * input_dx + input_dy * input_dy))
    
    g_buf = g_proxy.realize(im.width(), im.height())

    phi_init = Func('phi_init')
    phi_init[x,y] = select((x >= selectPadding)
                            & (x < im.width() - selectPadding)
                            & (y >= selectPadding)
                            & (y < im.height() - selectPadding),
                            -2.0, 2.0)

    phi_input = UniformImage(Float(32), 2)

    phi_clamped = Func('phi_clamped')
    phi_clamped[x,y] = phi_input[clamp(x,cast(int_t,0),cast(int_t,im.width()-1)),
                                 clamp(y,cast(int_t,0),cast(int_t,im.height()-1))]
  
    g_clamped = Func('g_clamped')
    g_clamped[x, y] = g_buf[clamp(x, cast(int_t,0), cast(int_t,im.width()-1)),
                            clamp(y, cast(int_t,0), cast(int_t,im.height()-1))]

    phi_new = Func('phi_new')
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
    
        masked = Func('masked')
        c = Var('c')
        masked[x,y,c] = select(phi_buf[x, y] < 0.0, im[x, y, c], im[x, y, c]/4)
        out = masked.realize(im.width(), im.height(), 3)
        print 'Total time %f secs'%(time.time()-T0)
        return out

    # Human reference schedule
    human_schedule = 'phi_new.root().parallel(iv1).vectorize(iv0,4)'
    tune_ref_schedules = {'human': human_schedule}
    tune_runner = 'snake_runner.cpp'
    tune_in_images = [DEFAULT_FILENAME]
    tune_out_type = UInt(8)

    import autotune
    autotune.Schedule.fromstring(phi_new, human_schedule).apply()
    
    return (im, phi_new, evaluate, locals())

def main():
    (input, out_func, evaluate, local_d) = filter_func()
    filter_image(input, out_func, DEFAULT_FILENAME, eval_func=evaluate)().show()
    
if __name__ == '__main__':
    main()

    