from cHalide import *

wrap = Expr

# ----------------------------------------------------
# Expr
# ----------------------------------------------------

Expr.__add__ = lambda x, y: add(x, wrap(y))
Expr.__sub__ = lambda x, y: sub(x, wrap(y))
Expr.__mul__ = lambda x, y: mul(x, wrap(y))
Expr.__div__ = lambda x, y: div(x, wrap(y))
Expr.__mod__ = lambda x, y: mod(x, wrap(y))
Expr.__and__  = lambda x, y: and_op(x, wrap(y))
Expr.__or__  = lambda x, y: or_op(x, wrap(y))

Expr.__radd__ = lambda y, x: add(wrap(x), y)
Expr.__rsub__ = lambda y, x: sub(wrap(x), y)
Expr.__rmul__ = lambda y, x: mul(wrap(x), y)
Expr.__rdiv__ = lambda y, x: div(wrap(x), y)
Expr.__rmod__ = lambda y, x: mod(wrap(x), y)
Expr.__rand__  = lambda y, x: and_op(wrap(x), y)
Expr.__ror__  = lambda y, x: or_op(wrap(x), y)

Expr.__neg__ = lambda x: neg(x)
Expr.__invert__  = lambda x: invert(x)

Expr.__lt__  = lambda x, y: lt(x, wrap(y))
Expr.__le__  = lambda x, y: le(x, wrap(y))
Expr.__eq__  = lambda x, y: eq(x, wrap(y))
Expr.__ne__  = lambda x, y: ne(x, wrap(y))
Expr.__gt__  = lambda x, y: gt(x, wrap(y))
Expr.__ge__  = lambda x, y: ge(x, wrap(y))

Expr.__iadd__ = lambda x, y: iadd(x, wrap(y))
Expr.__isub__ = lambda x, y: isub(x, wrap(y))
Expr.__imul__ = lambda x, y: imul(x, wrap(y))
Expr.__idiv__ = lambda x, y: idiv(x, wrap(y))

# ----------------------------------------------------
# Var, Func
# ----------------------------------------------------

for C in [Var, FuncRef]:
    C.__add__ = lambda x, y: add(wrap(x), wrap(y))
    C.__sub__ = lambda x, y: sub(wrap(x), wrap(y))
    C.__mul__ = lambda x, y: mul(wrap(x), wrap(y))
    C.__div__ = lambda x, y: div(wrap(x), wrap(y))
    C.__mod__ = lambda x, y: mod(wrap(x), wrap(y))
    C.__and__  = lambda x, y: and_op(wrap(x), wrap(y))
    C.__or__  = lambda x, y: or_op(wrap(x), wrap(y))

    C.__neg__ = lambda x: neg(wrap(x))
    C.__invert__  = lambda x: invert(wrap(x))

    C.__lt__  = lambda x, y: lt(wrap(x), wrap(y))
    C.__le__  = lambda x, y: le(wrap(x), wrap(y))
    C.__eq__  = lambda x, y: eq(wrap(x), wrap(y))
    C.__ne__  = lambda x, y: ne(wrap(x), wrap(y))
    C.__gt__  = lambda x, y: gt(wrap(x), wrap(y))
    C.__ge__  = lambda x, y: ge(wrap(x), wrap(y))

# ----------------------------------------------------
# Func
# ----------------------------------------------------

def raise_error(e):
    raise e
    
Func.__call__ = lambda self, *L: raise_error(ValueError('use f[x, y] = expr to initialize a function'))
Func.__setitem__ = lambda x, key, value: assign(call(x, *[wrap(y) for y in key]), wrap(value)) if isinstance(key,tuple) else assign(call(x, key), wrap(value))
Func.__getitem__ = lambda x, key: call(x, *[wrap(y) for y in key]) if isinstance(key,tuple) else call(x, key)
#Func.__call__ = lambda self, *args: call(self, [wrap(x) for x in args])

# ----------------------------------------------------
# UniformImage
# ----------------------------------------------------

#UniformImage.__setitem__ = lambda x, key, value: assign(call(x, *[wrap(y) for y in key]), wrap(value)) if isinstance(key,tuple) else assign(call(x, key), wrap(value))
UniformImage.__getitem__ = lambda x, key: call(x, *[wrap(y) for y in key]) if isinstance(key,tuple) else call(x, key)
UniformImage.assign = lambda x, y: assign(x, y)

# ----------------------------------------------------
# Image
# ----------------------------------------------------

for ImageT in [Image_int8, Image_int16, Image_int32, Image_uint8, Image_uint16, Image_uint32, Image_float32, Image_float64]:
    ImageT.save = lambda x, y: save_png(x, y)
    ImageT.assign = lambda x, y: assign(x, y)

def Image(typeval, *args):
    assert isinstance(typeval, Type)
    sig = (typeval.bits, typeval.isInt(), typeval.isUInt(), typeval.isFloat())
    if sig == (8, True, False, False):
        C = Image_int8
    elif sig == (16, True, False, False):
        C = Image_int16
    elif sig == (32, True, False, False):
        C = Image_int32
    elif sig == (8, False, True, False):
        C = Image_uint8
    elif sig == (16, False, True, False):
        C = Image_uint16
    elif sig == (32, False, True, False):
        C = Image_uint32
    elif sig == (32, False, False, True):
        C = Image_float32
    elif sig == (64, False, False, True):
        C = Image_float64
    else:
        raise ValueError('unimplemented Image type signature %r' % typeval)
    if len(args) == 0:
        return C
    elif len(args) == 1 and isinstance(args[0], str):
        return load_png(C(1), args[0])
    elif all(isinstance(x, int) for x in args):
        return C(*args)
    else:
        raise ValueError('unknown Image constructor arguments %r' % args)

# ----------------------------------------------------
# Test
# ----------------------------------------------------

import time

def test_core():
    input = UniformImage(UInt(16),2)
    var_x = Var('x')
    var_y = Var('y')
    blur_x = Func('blur_x')
    blur_y = Func('blur_y')
    expr_x = Expr(var_x)
    #print expr_x
    expr_y = Expr(var_y)
    #Expr(x) + Expr(y)

    T0 = time.time()
    n = 1

    def check(L):
        for x in L:
            assert isinstance(x, Expr), x

    for i in range(n):
        for x in [expr_x, var_x, 1, 1.3]:
            for y in [expr_y, var_y, 1, 1.3]:
                if isinstance(x, (int,float)) and isinstance(y, (int, float)):
                    continue
                #print type(x), type(y)
                check([x + y,
                x - y,
                x * y,
                x / y,
                x % y,
                x < y,
                x <= y,
                x == y,
                x != y,
                x > y,
                x >= y,
                x & y,
                x | y] + [-x, ~x] if not isinstance(x, (int, float)) else [])

                if isinstance(x, Expr):
                    x += y
                    check([x])
                    x -= y
                    check([x])
                    x *= y
                    check([x])
                    x /= y
                    check([x])
        
    #blur_x += expr_x
    #blur_x(expr_x)

    T1 = time.time()

    x = var_x
    y = var_y
    z = Var()
    q = Var()
    
    assert isinstance(x+1, Expr)
    assert isinstance(x/y, Expr)
    assert isinstance((x/y)+(x-1), Expr)
    assert isinstance(blur_x[x-1,y], FuncRef)
    assert isinstance(blur_x[x, y], FuncRef)
    assert isinstance(blur_x[x-1], FuncRef)
    assert isinstance(blur_x[x-1,y,z], FuncRef)
    assert isinstance(blur_x[x-1,y,z,q], FuncRef)
    f = Func()
    f[x,y]=x+1
    
    print 'halide core: OK'

def test_blur():
    input = UniformImage(UInt(16), 3, 'input')
    x = Var('x')
    y = Var('y')
    c = Var('c')
    blur_x = Func('blur_x')
    blur_y = Func('blur_y')
    
    blur_x[x,y,c] = (input[x-1,y,c]/4+input[x,y,c]/4+input[x+1,y,c]/4)/3
    blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4

#blur_x[x,y,c] = (input[x-1,y,c]/4+input[x,y,c]/4+input[x+1,y,c]/4)/3
#blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4

    for f in [blur_y, blur_x]:
        assert f.args()[0].vars()[0].name()=='x'
        assert f.args()[1].vars()[0].name()=='y'
        assert f.args()[2].vars()[0].name()=='c'
        assert len(f.args()) == 3
    assert blur_y.rhs().funcs()[0].name()=='blur_x'
    assert len(blur_y.rhs().funcs()) == 1
    assert blur_x.rhs().uniformImages()[0].name()
    assert len(blur_x.rhs().uniformImages()) == 1

    #blur_y.parallel(y)
    blur_y.unroll(y,16).vectorize(x,16)
    blur_y.compileToFile('halide_blur')
    blur_y.compileJIT()

    input_png = Image(UInt(16), 'lena.png')
    input.assign(input_png)
    w = input_png.width()
    h = input_png.height()
    nchan = input_png.channels()
    out = Image(UInt(16), w, h, nchan)
    T0 = time.time()
    out.assign(blur_y.realize(w, h, nchan))
    T1 = time.time()
    print T1-T0
    
    out.save('out.png')
    
    print 'halide blur: OK'
    
def test():
    test_core()
    test_blur()

if __name__ == '__main__':
    test()
    