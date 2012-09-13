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
     clamp(y,cast(Int(32),0),cast(Int(32),input.height()-1)), c]
blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4

out_func = blur_y

if False:
    blur_x.root().parallel(y)
    blur_y.root().parallel(c)
else:
    blur_y.tile(x, y, xi, yi, 8, 8)
    blur_y.vectorize(xi, 8)
    blur_y.parallel(y)
    blur_x.chunk(x)
    blur_x.vectorize(x, 8)

evaluate = filter_image(input, out_func, 'apollo2.png')
evaluate()
T0 = time.time()
evaluate()
T1 = time.time()

print T1-T0

