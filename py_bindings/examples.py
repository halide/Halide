
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

    input_clamped[x,y,c] = input[clamp(Expr(x),cast(Int(32),0),cast(Int(32),input.width()-1)),
                                 clamp(Expr(y),cast(Int(32),0),cast(Int(32),input.height()-1)), c]
    subexp = input_clamped[x,y]
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            if dx != 0 or dy != 0:
                subexp = min(subexp, input_clamped[x+dx,y+dy])
    dilate[x,y,c] = subexp #min(min(input_clamped[x-1,y-1,c],input_clamped[x,y-1,c]),input_clamped[x+1,y-1,c])
    counter[0] += 1
    return (input, dilate)
