from halide import *
f = Func('f')
x = Var('x')
y = Var('y')
c = Var('c')
g = Func('g')
_c0 = Var('_c0')    # Auxiliary variables introduced by the 'enumerate all valid schedules' algorithm
_c1 = Var('_c1')
_c2 = Var('_c2')
_c3 = Var('_c3')
input = UniformImage(UInt(16), 3)
int_t = Int(32)
f[x,y,c] = input[clamp(x,cast(int_t,0),cast(int_t,input.width()-1)),
                 clamp(y,cast(int_t,0),cast(int_t,input.height()-1)),
                 clamp(c,cast(int_t,0),cast(int_t,2))]
g[x,y,c] = f[x,y,c]+1

# Paste schedule on the next line
g.root().vectorize(c,2)

shape = (30, 30, 3)
in_image = Image(numpy.zeros(shape, input.type().to_numpy()))
input.assign(in_image)
out = Image(input.type(), in_image.width(), in_image.height(), in_image.channels())
g.compileJIT()
out.assign(g.realize(in_image.width(), in_image.height(), in_image.channels()))

print 'Success'

