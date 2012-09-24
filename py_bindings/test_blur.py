from halide import *

dtype = UInt(16)

counter = [0]
s = '_blur%d'%counter[0]
input = UniformImage(dtype, 3, 'input'+s)
x = Var('x'+s)
y = Var('y'+s)
c = Var('c'+s)
input_clamped = Func('input_clamped'+s)
blur_x = Func('blur_x'+s)
blur_y = Func('blur_y'+s)

xi = Var('xi')
yi = Var('yi')

input_clamped[x,y,c] = input[clamp(x,cast(Int(32),0),cast(Int(32),input.width()-1)),
     clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)),
     clamp(c,cast(Int(32),0),cast(Int(32),2))]
blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4

out_func = blur_y

schedule = 2

if schedule == 0: #False:
    blur_x.root().parallel(y)
    blur_y.root().parallel(y)
elif schedule == 1:    # Uncomment all lines for fastest schedule
    blur_y.tile(x, y, xi, yi, 8, 8)
    blur_y.vectorize(xi, 8)
    blur_y.parallel(y)
    blur_x.chunk(x)
    blur_x.vectorize(x, 8)
elif schedule == 2:
    _c0 = Var('_c0')
    _c1 = Var('_c1')
#    blur_x.root().unroll(c,32) #.tile(y,c,_c0,_c1,64,64)
#    blur_y.root().split(x,x,_c0,64).unroll(x,8)
    blur_y.root().tile(y,c,_c0,_c1,64,8) #.parallel(c)

evaluate = filter_image(input, out_func, 'lena_crop.png')
print 'Compiled'
evaluate()
T0 = time.time()
evaluate()
T1 = time.time()

print T1-T0

