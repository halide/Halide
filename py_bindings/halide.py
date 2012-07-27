from cHalide import *
import numpy
import Image as PIL
import os
import signal
from ForkedWatchdog import Watchdog

#exit_on_signal()

wrap = Expr

in_filename = 'lena_crop.png' #'lena.png' #'lena_crop.png'

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

def image_getattr(self, name):
    if name == '__array_interface__':
       # print 'get_array'
        #print 'get array'
        #return {'shape': (1,1), 'typestr': '>i4', 'data': '\x00'*3+'\x02'}
        D = DynImage(self)
        t = D.type()
        if t.isInt():
            typestr = '|i%d'%(t.bits/8)
        elif t.isUInt():
            typestr = '|u%d'%(t.bits/8)
        elif t.isFloat():
            typestr = '|f%d'%(t.bits/8)
        else:
            raise ValueError('Unknown type %r'%t)
        shape = tuple([D.size(i) for i in range(D.dimensions())])
        strides = tuple([D.stride(i)*(t.bits/8) for i in range(D.dimensions())])
        data = image_to_string(self)
     #   print 'size', len(data)
        return {'shape': shape,
                'typestr': typestr,
                'data': data,
                'strides': strides}
    raise AttributeError(name)

ImageTypes = (Image_int8, Image_int16, Image_int32, Image_uint8, Image_uint16, Image_uint32, Image_float32, Image_float64)

def show_image(I):
    A = numpy.asarray(I)
  #  print 'converted to numpy', A.dtype
    if A.dtype == numpy.uint8:
        pass
    elif A.dtype == numpy.uint16: #numpy.issubdtype(A.dtype, 'uint16'):
        A = numpy.array(A/256, 'uint8')
    elif A.dtype == numpy.uint32: #numpy.issubdtype(A.dtype, 'uint32'):
        A = numpy.array(A/256**3, 'uint8')
    elif A.dtype == numpy.float32 or A.dtype == numpy.float64: #numpy.issubdtype(A.dtype, 'float32') or numpy.issubdtype(A.dtype, 'float64'):
        A = numpy.array(A*255.0, 'uint8')
    else:
        raise ValueError('Unsupported dtype %r' % A.dtype)
   # print 'showing'
    A = numpy.transpose(A, [1, 0] if len(A.shape) == 2 else [1, 0, 2])
    if len(A.shape) == 3 and A.shape[2] == 1:
        A = A[:,:,0]
    PIL.fromarray(A).show()
    
for ImageT in ImageTypes:
    ImageT.save = lambda x, y: save_png(x, y)
    ImageT.assign = lambda x, y: assign(x, y)
    ImageT.__getattr__ = image_getattr
    ImageT.show = lambda x: show_image(x)
    
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
#        ans_img = C(1)
#        print 'load_png'
        return load_png(C(1), args[0])
    elif all(isinstance(x, int) for x in args):
        return C(*args)
    else:
        raise ValueError('unknown Image constructor arguments %r' % args)

# ----------------------------------------------------
# Uniform
# ----------------------------------------------------

UniformTypes = (Uniform_int8, Uniform_int16, Uniform_int32, Uniform_uint8, Uniform_uint16, Uniform_uint32, Uniform_float32, Uniform_float64)

def Uniform(typeval, *args):
    assert isinstance(typeval, Type)
    sig = (typeval.bits, typeval.isInt(), typeval.isUInt(), typeval.isFloat())
    if sig == (8, True, False, False):
        C = Uniform_int8
    elif sig == (16, True, False, False):
        C = Uniform_int16
    elif sig == (32, True, False, False):
        C = Uniform_int32
    elif sig == (8, False, True, False):
        C = Uniform_uint8
    elif sig == (16, False, True, False):
        C = Uniform_uint16
    elif sig == (32, False, True, False):
        C = Uniform_uint32
    elif sig == (32, False, False, True):
        C = Uniform_float32
    elif sig == (64, False, False, True):
        C = Uniform_float64
    else:
        raise ValueError('unimplemented Uniform type signature %r' % typeval)
    if len(args) == 1:          # Handle special cases since SWIG apparently cannot convert int32_t to int.
        if isinstance(args[0], (int, float)):
            ans = C()
            assign(ans, args[0])
            return ans
    elif len(args) == 2:
        if isinstance(args[1], (int, float)):
            ans = C(args[0])
            assign(ans, args[1])
            return ans
    return C(*args)

for UniformT in UniformTypes:
    UniformT.assign = lambda x, y: assign(x, y)

# ----------------------------------------------------
# DynImage
# ----------------------------------------------------

DynImageType = DynImage
def DynImage(*args):
    if len(args) == 1 and isinstance(args[0], ImageTypes):
        return to_dynimage(args[0])
    return DynImageType(*args)
    
# ----------------------------------------------------
# Repr
# ----------------------------------------------------

Var.__repr__ = lambda self: 'Var(%r)'%self.name()
Type.__repr__ = lambda self: 'UInt(%d)'%self.bits if self.isUInt() else 'Int(%d)'%self.bits if self.isInt() else 'Float(%d)'%self.bits if self.isFloat() else 'Type(%d)'%self.bits 
Expr.__repr__ = lambda self: 'Expr(%s)' % ', '.join([repr(self.type())] + [str(_x) for _x in self.vars()])
Func.__repr__ = lambda self: 'Func(%r)' % self.name()

# ----------------------------------------------------
# Global functions
# ----------------------------------------------------

_clamp = clamp
_min = min
_max = max
_cast = cast
clamp = lambda x, y, z: _clamp(wrap(x), wrap(y), wrap(z))
min   = lambda x, y: _min(wrap(x), wrap(y))
max   = lambda x, y: _max(wrap(x), wrap(y))
cast  = lambda x, y: _cast(x, wrap(y))

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

def all_funcs(f):
    d = {}
    def visit(x):
        name = x.name()
        if name not in d:
            d[name] = x
            for y in f.rhs().funcs():
                visit(y)
    visit(f)
    return d

def func_varlist(f):
    args = f.args()
    return [x.vars()[0].name() for x in args]

def get_blur(cache=[]):
    def gen():
        input = UniformImage(UInt(16), 3, 'input')
        x = Var('x')
        y = Var('y')
        c = Var('c')
        input_clamped = Func('input_clamped')
        blur_x = Func('blur_x')
        blur_y = Func('blur_y')

        #print 'before input_clamped'
        input_clamped[x,y,c] = input[clamp(Expr(x),cast(Int(32),Expr(0)),cast(Int(32),Expr(input.width()-1))),
                                     clamp(Expr(y),cast(Int(32),Expr(0)),cast(Int(32),Expr(input.height()-1))),
                                     c] #clamp(Expr(c),Expr(0),Expr(input.width()-1))]
        #print 'after input_clamped'
        #input_clamped[x,y,c] = input[x,y,c]
        blur_x[x,y,c] = (input_clamped[x-1,y,c]/4+input_clamped[x,y,c]/4+input_clamped[x+1,y,c]/4)/3
    #    blur_x[x,y,c] = (input[x-1,y,c]/4+input[x,y,c]/4+input[x+1,y,c]/4)/3
        blur_y[x,y,c] = (blur_x[x,y-1,c]+blur_x[x,y,c]+blur_x[x,y+1,c])/3*4
        return (input, x, y, c, blur_x, blur_y, input_clamped)
    
    # Reuse global copy
    if len(cache) == 0:
        cache.append(gen())
    (input, x, y, c, blur_x, blur_y, input_clamped) = cache[0]
    
    # Reset schedules
    input_clamped.reset()
    blur_x.reset()
    blur_y.reset()
    
    return (input, x, y, c, blur_x, blur_y, input_clamped)

def roundup_multiple(x, y):
    return (x+y-1)/y*y

def filter_filename(input, out_func, filename, dtype=UInt(16)): #, pad_multiple=1):
    """
    Utility function to filter an image file with a Halide Func, returning output Image of the same size.
    
    Given input and output Funcs, and filename, returns evaluate. Calling evaluate() returns the output Image.
    """
    input_png = Image(dtype, filename)
    print input_png.dimensions()
#    print [input_png.size(i) for i in range(input_png.dimensions())]
    #print 'assign'
    input.assign(input_png)
    #print 'get w'
    w = input_png.width()
    h = input_png.height()
    nchan = input_png.channels()
    #print w, h, nchan
    #w2 = roundup_multiple(w, pad_multiple)
    #h2 = roundup_multiple(h, pad_multiple)
    #print w2, h2, nchan
    out = Image(dtype, w, h, nchan)
    
    def evaluate():
        out.assign(out_func.realize(w, h, nchan))
        return out
    return evaluate
    
def example_out():
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()

    blur_y.reset().unroll(y,16).vectorize(x,16)
    blur_y.compileJIT()

    return filter_filename(input, blur_y, in_filename)()
    
def test_blur():
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()
    
    for f in [blur_y, blur_x]:
        assert f.args()[0].vars()[0].name()=='x'
        assert f.args()[1].vars()[0].name()=='y'
        assert f.args()[2].vars()[0].name()=='c'
        assert len(f.args()) == 3
    assert blur_y.rhs().funcs()[0].name()=='blur_x'
    assert blur_x.rhs().uniformImages()[0].name()
    assert len(blur_x.rhs().uniformImages()) == 1

    #print [str(x) for x in blur_y.rhs().funcs()]
    assert len(blur_y.rhs().funcs()) == 2
    assert set(all_funcs(blur_y).keys()) == set(['blur_x', 'blur_y', 'input_clamped']), set(all_funcs(blur_y).keys())
    assert func_varlist(blur_y) == ['x', 'y', 'c'], func_varlist(blur_y)
    assert func_varlist(blur_x) == ['x', 'y', 'c'], func_varlist(blur_x)
    
    #blur_y.parallel(y)
    for i in range(2):
        #print 'iter', i
#        blur_y.reset().unroll(y,16).vectorize(x,16)
        #print 'c'
        #blur_y.root().serial(x).serial(y).unroll(y,16).vectorize(x,16)
        blur_y.compileToFile('halide_blur')
        #print 'd'
        blur_y.compileJIT()
        #print 'e'

        outf = filter_filename(input, blur_y, in_filename)
        #print 'f'
        T0 = time.time()
        out = outf()
        T1 = time.time()
        #print 'g'
        print T1-T0, 'secs'
    
        out.save('out2.png' if i==1 else 'out.png')
        s = image_to_string(out)
        assert isinstance(s, str)
        #print numpy.asarray(out)
        #PIL.fromarray(out).show()
        out.show()
    I1 = numpy.asarray(PIL.open('out.png'))
    I2 = numpy.asarray(PIL.open('out2.png'))
    os.remove('out2.png')
    
    print 'halide blur: OK'

def test_func():
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()

    outf = filter_filename(input, blur_y, in_filename)
    
    def test(func):
        T0 = time.time()
        out = outf()
        T1 = time.time()
        return T1-T0
    
    return locals()

def test_autotune():
    locals_d = test_func()
    
    import halide_autotune
    halide_autotune.autotune(locals_d['blur_y'], locals_d['test'], locals_d)

def test_segfault():
    locals_d = test_func()
    c = locals_d['c']
    blur_y = locals_d['blur_y']
    test = locals_d['test']
    
    """
    def signal_handler(signal, frame):
        #print 'Caught signal, exiting'
        #sys.stdout.flush()
        sys.exit(0)
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGSEGV, signal_handler)
    signal.signal(signal.SIGILL, signal_handler)
    signal.signal(signal.SIGABRT, signal_handler)
    signal.signal(signal.SIGFPE, signal_handler)
    """
    exit_on_signal()
    
    blur_y.unroll(c, 32)
    print 'Before test'
    try:
        with Watchdog(5):
            test(blur_y)
            print 'halide segfault: Failed to segfault'
            sys.exit(0)
    except Watchdog:
        pass
    print 'halide segfault: OK'

def test_examples():
    import examples
    for example in [examples.blur, examples.dilate]:
        for input_image in ['lena_crop_grayscale.png', 'lena_crop.png']:
            for dtype in [UInt(8), UInt(16), UInt(32), Float(32), Float(64)]:
        #        (in_func, out_func) = examples.blur_color(dtype)
    #            (in_func, out_func) = examples.blur(dtype)
                (in_func, out_func) = example(dtype)
        #        print 'got func'
        #        outf = filter_filename(in_func, out_func, in_filename, dtype)
                outf = filter_filename(in_func, out_func, input_image, dtype)
        #        print 'got filter'
                out = outf()
        #        print 'filtered'
                out.show()
        #        print 'shown'
                A = numpy.asarray(out)
                print numpy.min(A.flatten()), numpy.max(A.flatten())
        #        out.save('out.png')

def test():
    exit_on_signal()
#    print 'a'
#    uint16 = UInt(16)
#    print 'b'
#    input = UniformImage(uint16, 3, 'input')
#    print 'c'
#    pass

    test_core()
    test_segfault()
    test_blur()
    test_examples()
    print 'Done testing'
    #test_autotune()
    
if __name__ == '__main__':
    test()
    
