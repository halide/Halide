
# TODO:
# - cpp_bindings/Halide.h seems to be missing in the repo, figure out what is wrong
# - snake has an error in the test

from cHalide import *
import numpy
import Image as PIL
import os
import sys
import signal
from ForkedWatchdog import Watchdog

#exit_on_signal()

# ----------------------------------------------------------------------------------------------------------
# Types (functions are used to replace the "constructors" due to an issue with SWIG replacing __new__)
# ----------------------------------------------------------------------------------------------------------

DynUniformType = DynUniform
ExprType = Expr
UniformTypes = (Uniform_int8, Uniform_int16, Uniform_int32, Uniform_uint8, Uniform_uint16, Uniform_uint32, Uniform_float32, Uniform_float64)
RDomType = RDom
ImageTypes = (Image_int8, Image_int16, Image_int32, Image_uint8, Image_uint16, Image_uint32, Image_float32, Image_float64)
UniformImageType = UniformImage
DynImageType = DynImage

# ----------------------------------------------------
# Expr
# ----------------------------------------------------

#wrap = lambda *a: Expr(*a) if not (len(a) == 1 and isinstance(a[0], UniformTypes)) else Expr(DynUniform(a[0]))
def wrap(*a):
#    print a
    if len(a) == 1:
        if isinstance(a[0], UniformTypes):
            return ExprType(DynUniform(a[0]))
        elif isinstance(a[0], ImageTypes) or isinstance(a[0], DynImageType):
            return a[0] #ExprType(to_dynimage(a[0]))
        elif isinstance(a[0], (int,long)):
            return expr_from_int(a[0])
        elif isinstance(a[0], tuple):
            return expr_from_tuple(*(wrap(x) for x in a[0]))
    return ExprType(*a)
    
in_filename = 'lena_crop.png' #'lena.png' #'lena_crop.png'

#_expr_new = Expr.__new__
#def expr_new(cls, *args):
#    if isinstance(args[0], UniformTypes):
#        return _expr_new(cls, DynUniform(args[0]))
#    return _expr_new(cls, *args)

#Expr.__new__ = expr_new

#def iadd2(a, b):
#    print 'iadd2', a, b
#    try:
#        iadd(a, b)
#    except ValueError:
#        print 'ValueError'
    
for BaseT in (Expr, FuncRef, Var, RDom, RVar, Func) + UniformTypes:
    BaseT.__add__ = lambda x, y: add(wrap(x), wrap(y))
    BaseT.__sub__ = lambda x, y: sub(wrap(x), wrap(y))
    BaseT.__mul__ = lambda x, y: mul(wrap(x), wrap(y))
    BaseT.__div__ = lambda x, y: div(wrap(x), wrap(y))
    BaseT.__mod__ = lambda x, y: mod(wrap(x), wrap(y))
    BaseT.__pow__ = lambda x, y: pow(wrap(x), wrap(y))
    BaseT.__and__  = lambda x, y: and_op(wrap(x), wrap(y))
    BaseT.__or__  = lambda x, y: or_op(wrap(x), wrap(y))

    BaseT.__radd__ = lambda y, x: add(wrap(x), wrap(y))
    BaseT.__rsub__ = lambda y, x: sub(wrap(x), wrap(y))
    BaseT.__rmul__ = lambda y, x: mul(wrap(x), wrap(y))
    BaseT.__rdiv__ = lambda y, x: div(wrap(x), wrap(y))
    BaseT.__rmod__ = lambda y, x: mod(wrap(x), wrap(y))
    BaseT.__rpow__ = lambda y, x: pow(wrap(x), wrap(y))
    BaseT.__rand__  = lambda y, x: and_op(wrap(x), wrap(y))
    BaseT.__ror__  = lambda y, x: or_op(wrap(x), wrap(y))

    BaseT.__neg__ = lambda x: neg(wrap(x))
    BaseT.__invert__  = lambda x: invert(wrap(x))

    BaseT.__lt__  = lambda x, y: lt(wrap(x), wrap(y))
    BaseT.__le__  = lambda x, y: le(wrap(x), wrap(y))
    BaseT.__eq__  = lambda x, y: eq(wrap(x), wrap(y))
    BaseT.__ne__  = lambda x, y: ne(wrap(x), wrap(y))
    BaseT.__gt__  = lambda x, y: gt(wrap(x), wrap(y))
    BaseT.__ge__  = lambda x, y: ge(wrap(x), wrap(y))

    BaseT.__iadd__ = lambda x, y: iadd(wrap(x), wrap(y))
    BaseT.__isub__ = lambda x, y: isub(wrap(x), wrap(y))
    BaseT.__imul__ = lambda x, y: imul(wrap(x), wrap(y))
    BaseT.__idiv__ = lambda x, y: idiv(wrap(x), wrap(y))

# ----------------------------------------------------
# RDom
# ----------------------------------------------------

def RDom(*args):
    args = [wrap(x) if not isinstance(x,str) else x for x in args]
    return RDomType(*args)

# ----------------------------------------------------
# Var, Func
# ----------------------------------------------------

for C in [Var, FuncRef]:
    C.__add__ = lambda x, y: add(wrap(x), wrap(y))
    C.__sub__ = lambda x, y: sub(wrap(x), wrap(y))
    C.__mul__ = lambda x, y: mul(wrap(x), wrap(y))
    C.__div__ = lambda x, y: div(wrap(x), wrap(y))
    C.__mod__ = lambda x, y: mod(wrap(x), wrap(y))
    C.__pow__ = lambda x, y: pow(wrap(x), wrap(y))
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

_generic_getitem = lambda x, key: call(x, *[wrap(y) for y in key]) if isinstance(key,tuple) else call(x, wrap(key))
_generic_assign = lambda x, y: assign(x, wrap(y))
_realize = Func.realize
_split = Func.split
_tile = Func.tile
Func.__call__ = lambda self, *L: raise_error(ValueError('used f(x, y) to refer to a Func -- proper syntax is f[x, y]'))
Func.__setitem__ = lambda x, key, value: assign(call(x, *[wrap(y) for y in key]), wrap(value)) if isinstance(key,tuple) else assign(call(x, wrap(key)), wrap(value))
Func.__getitem__ = _generic_getitem
Func.assign = _generic_assign
Func.realize = lambda x, *a: _realize(x,*a) if not (len(a)==1 and isinstance(a[0], ImageTypes)) else _realize(x,to_dynimage(a[0]))
Func.split = lambda self, a, b, c, d: _split(self, a, b, c, wrap(d))
Func.tile = lambda self, *a: _tile(self, *[a[i] if i < len(a)-2 else wrap(a[i]) for i in range(len(a))])

#Func.__call__ = lambda self, *args: call(self, [wrap(x) for x in args])

# ----------------------------------------------------
# FuncRef
# ----------------------------------------------------

#FuncRef.__mul__ = lambda x, y: mul(wrap(x), wrap(y))
#FuncRef.__rmul__ = lambda x, y: mul(wrap(x), wrap(y))

# ----------------------------------------------------
# Image
# ----------------------------------------------------

# Halide examples use f[x,y] convention -- flip to I[y,x] convention when converting Halide <=> numpy
flip_xy = True

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
        if flip_xy and len(strides) >= 2:
            strides = (strides[1], strides[0]) + strides[2:]
            shape = (shape[1], shape[0]) + shape[2:]
        data = image_to_string(self)
     #   print 'size', len(data)
        return {'shape': shape,
                'typestr': typestr,
                'data': data,
                'strides': strides}
    raise AttributeError(name)

for _ImageT in ImageTypes:
    _ImageT.__getitem__ = _generic_getitem

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
#    A = numpy.transpose(A, [1, 0] if len(A.shape) == 2 else [1, 0, 2])
    if len(A.shape) == 3 and A.shape[2] == 1:
        A = A[:,:,0]
    PIL.fromarray(A).show()
    
for _ImageT in ImageTypes:
    _ImageT.save = lambda x, y: save_png(x, y)
    _ImageT.assign = _generic_assign
    _ImageT.__getattr__ = image_getattr
    _ImageT.show = lambda x: show_image(x)

def _image_from_numpy(a):
    a = numpy.asarray(a)
    shape = a.shape
    strides = a.strides
    if flip_xy and len(shape) >= 2:
        shape = (shape[1], shape[0]) + shape[2:]
        strides = (strides[1], strides[0]) + strides[2:]
        
    d = {numpy.dtype('int8'): Image_int8,
         numpy.dtype('int16'): Image_int16,
         numpy.dtype('int32'): Image_int32,
         numpy.dtype('uint8'): Image_uint8,
         numpy.dtype('uint16'): Image_uint16,
         numpy.dtype('uint32'): Image_uint32,
         numpy.dtype('float32'): Image_float32,
         numpy.dtype('float64'): Image_float64}

    if a.dtype in d:
        C = d[a.dtype]
    else:
        raise TypeError('No Image constructor for numpy.%r'%a.dtype)
    
    ans = C(*shape)
    assign_array(ans, a.__array_interface__['data'][0], *strides)
    return ans
    
def Image(typeval, *args):
    """
    Constructors:
    Image(typeval),    typeval=Int(n), UInt(n), Float(n)
    Image(typeval, png_filename)
    Image(typeval, nsize)
    Image(typeval, w, h)
    Image(typeval, w, h, nchan)
    ...
    Image(typeval, DynImage)
    Image(typeval, UniformImage)
    Image(numpy_array)
    """
    if len(args) == 0 and hasattr(typeval, '__len__'):
        return _image_from_numpy(typeval)

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
    elif len(args) == 1 and isinstance(args[0], ImageTypes+(DynImageType,UniformImageType)):
        return C(*args)
    else:
        raise ValueError('unknown Image constructor arguments %r' % args)

# ----------------------------------------------------
# Uniform
# ----------------------------------------------------

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

for UniformT in UniformTypes + (DynUniform,):
    UniformT.assign = lambda x, y: assign(x, y) #_generic_assign

# ----------------------------------------------------
# DynImage
# ----------------------------------------------------

def DynImage(*args):
    if len(args) == 1 and isinstance(args[0], ImageTypes):
        return to_dynimage(args[0])
    return DynImageType(*args)

# ----------------------------------------------------
# DynUniform
# ----------------------------------------------------

def DynUniform(*args):
    if len(args) == 1 and isinstance(args[0], UniformTypes):
        return to_dynuniform(args[0])
    return DynUniformType(*args)

# ----------------------------------------------------
# Various image types
# ----------------------------------------------------

#UniformImage.__setitem__ = lambda x, key, value: assign(call(x, *[wrap(y) for y in key]), wrap(value)) if isinstance(key,tuple) else assign(call(x, key), wrap(value))

for _ImageT in [DynImageType, UniformImage]:
    _ImageT.__getitem__ = _generic_getitem
    _ImageT.assign = _generic_assign
    #_ImageT.save = lambda x, y: save_png(x, y)

# ----------------------------------------------------
# Type
# ----------------------------------------------------

def _type_maxval(typeval):          # The typical maximum value used for image processing (1.0 for float types)
    if typeval.isUInt():
        return 2**(typeval.bits)-1
    elif typeval.isInt():
        return 2**(typeval.bits-1)-1
    elif typeval.isFloat():
        return 1.0
    else:
        raise ValueError('unknown typeval %r'%typeval)

def _type_to_numpy(typeval):
    if typeval.isInt():
        return numpy.dtype('int%d'%typeval.bits)
    elif typeval.isUInt():
        return numpy.dtype('uint%d'%typeval.bits)
    elif typeval.isFloat():
        return numpy.dtype('float%d'%typeval.bits)
    else:
        raise ValueError('unknown type %r'%typeval)

Type.maxval = _type_maxval
Type.to_numpy = _type_to_numpy

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

_sqrt  = sqrt
_sin   = sin
_cos   = cos
_exp   = exp
_log   = log
_floor = floor

_debug = debug

_select = select

_max = max
_min = min
_clamp = clamp

_cast = cast

sqrt   = lambda x: _sqrt(wrap(x))
sin    = lambda x: _sin(wrap(x))
cos    = lambda x: _cos(wrap(x))
exp    = lambda x: _exp(wrap(x))
log    = lambda x: _log(wrap(x))
floor  = lambda x: _floor(wrap(x))

debug  = lambda x, y, *a: _debug(wrap(x), y, *a)

select = lambda x, y, z: _select(wrap(x), wrap(y), wrap(z))

max    = lambda x, y: _max(wrap(x), wrap(y))
min    = lambda x, y: _min(wrap(x), wrap(y))
clamp  = lambda x, y, z: _clamp(wrap(x), wrap(y), wrap(z))

cast   = lambda x, y: _cast(x, wrap(y))

minimum = lambda x: minimum_func(wrap(x))
maximum = lambda x: maximum_func(wrap(x))
product = lambda x: product_func(wrap(x))
sum     = lambda x: sum_func(wrap(x))

# ----------------------------------------------------
# Constructors
# ----------------------------------------------------

Expr = wrap

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
            assert isinstance(x, ExprType), x

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

                if isinstance(x, ExprType):
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
    
    assert isinstance(x+1, ExprType)
    assert isinstance(x/y, ExprType)
    assert isinstance((x/y)+(x-1), ExprType)
    assert isinstance(blur_x[x-1,y], FuncRef)
    assert isinstance(blur_x[x, y], FuncRef)
    assert isinstance(blur_x[x-1], FuncRef)
    assert isinstance(blur_x[x-1,y,z], FuncRef)
    assert isinstance(blur_x[x-1,y,z,q], FuncRef)
    f = Func()
    f[x,y]=x+1
    
    print 'halide core: OK'

def visit_funcs(root_func, callback):
    "Call callback(f, fparent) recursively (DFS) on all functions reachable from root_func."
    d = {}
    def visit(x, parent):
        name = x.name()
        if name not in d:
            d[name] = x
            callback(x, parent)
            #print x.rhs().funcs()
            for y in x.rhs().funcs():
                visit(y, x)
    visit(root_func, None)
    return d

def all_funcs(root_func):
    d = {}
    def callback(f, parent):
        d[f.name()] = f
    visit_funcs(root_func, callback)
    return d

def all_vars(root_func):
    d = {}
    def callback(f, parent):
        for x in f.args():
            var = x.vars()[0]
            d[var.name()] = var
    visit_funcs(root_func, callback)
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

def schedule_all(f, schedule):
    "Call schedule(f), and recursively call schedule on all functions referenced by f."
    for g in all_funcs(f).values():
        schedule(g)
        
def root_all(f):
    "Schedule f and all functions referenced by f as root."
    schedule_all(f, lambda fn: fn.root())

def inline_all(f):
    "Schedule f and all functions referenced by f as inline."
    schedule_all(f, lambda fn: fn.reset())

def roundup_multiple(x, y):
    return (x+y-1)/y*y

def filter_image(input, out_func, in_image, disp_time=False, compile=True, eval_func=None): #, pad_multiple=1):
    """
    Utility function to filter an image filename or numpy array with a Halide Func, returning output Image of the same size.
    
    Given input and output Funcs, and filename/numpy array (in_image), returns evaluate. Calling evaluate() returns the output Image.
    """
    dtype = input.type()
    if isinstance(input, UniformImageType):
        if isinstance(in_image, str):
            input_png = Image(dtype, in_image)
        else:
            input_png = Image(in_image)
    #print input_png.dimensions()
#    print [input_png.size(i) for i in range(input_png.dimensions())]
    #print 'assign'
        input.assign(input_png)
    else:
        input_png = input
    #print 'get w'
    w = input_png.width()
    h = input_png.height()
    nchan = input_png.channels()
    #print w, h, nchan
    #w2 = roundup_multiple(w, pad_multiple)
    #h2 = roundup_multiple(h, pad_multiple)
    #print w2, h2, nchan
    out = Image(dtype, w, h, nchan)
    if compile:
        out_func.compileJIT()

    def evaluate():
        T0 = time.time()
        try:
            if eval_func is not None:
                out.assign(eval_func(input_png))
                return out
            else:
                print 'a'
                realized = out_func.realize(w, h, nchan)
                print 'b'
                out.assign(realized)
                print 'c'
                return out
        finally:
            assert out.width() == w and out.height() == h and out.channels() == nchan
            #print out.width(), out.height(), out.channels(), w, h, nchan
            if disp_time:
                print 'Filtered in', time.time()-T0, 'secs'
    return evaluate

def example_out():
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()

    blur_y.reset().unroll(y,16).vectorize(x,16)
    blur_y.compileJIT()

    return filter_image(input, blur_y, in_filename)()
    
def test_blur():
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()
    
    for f in [blur_y, blur_x]:
        assert f.args()[0].vars()[0].name()=='x'
        assert f.args()[1].vars()[0].name()=='y'
        assert f.args()[2].vars()[0].name()=='c'
        assert len(f.args()) == 3
    assert blur_y.rhs().funcs()[0].name()=='blur_x'
#    assert blur_x.rhs().uniformImages()[0].name()
#    assert len(blur_x.rhs().uniformImages()) == 1

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
        #blur_y.compileJIT()
        #print 'e'

        outf = filter_image(input, blur_y, in_filename)
        #print 'f'
        T0 = time.time()
        out = outf()
        T1 = time.time()
        #print 'g'
        #print T1-T0, 'secs'
    
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

def test_func(compile=True, in_image=in_filename):
    (input, x, y, c, blur_x, blur_y, input_clamped) = get_blur()

    outf = filter_image(input, blur_y, in_image, compile=compile)
    out = [None]
    
    def test(func):
        T0 = time.time()
        out[0] = outf()
        T1 = time.time()
        return T1-T0
    
    return locals()

def test_autotune():
    locals_d = test_func()
    
    import petabricks_autotune
    petabricks_autotune.autotune(locals_d['blur_y'], locals_d['test'], locals_d)

def test_segfault():
    locals_d = test_func(compile=False)
    x = locals_d['x']
    y = locals_d['y']
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
    
#    blur_y.unroll(c, -320)
#    blur_y.vectorize(c, -320)
#    blur_y.transpose(x, y)
    print 'Before test'
    try:
        with Watchdog(5):
            test(blur_y)
            blur_y.update()
            print 'halide segfault: Failed to segfault'
            sys.exit(0)
    except Watchdog:
        pass
    print 'halide segfault: OK'

def test_examples():
    import examples
    in_grayscale = 'lena_crop_grayscale.png'
    in_color = 'lena_crop.png'
    
    names = []
    do_filter = True
    
#    for example_name in ['interpolate']: #
    for example_name in 'interpolate blur dilate boxblur_sat boxblur_cumsum local_laplacian'.split(): #[examples.snake, examples.blur, examples.dilate, examples.boxblur_sat, examples.boxblur_cumsum, examples.local_laplacian]:
#    for example_name in 'interpolate blur dilate boxblur_sat boxblur_cumsum local_laplacian snake'.split(): #[examples.snake, examples.blur, examples.dilate, examples.boxblur_sat, examples.boxblur_cumsum, examples.local_laplacian]:
#    for example_name in 'interpolate snake blur dilate boxblur_sat boxblur_cumsum local_laplacian'.split(): #[examples.snake, examples.blur, examples.dilate, examples.boxblur_sat, examples.boxblur_cumsum, examples.local_laplacian]:
        example = getattr(examples, example_name)
        first = True
#    for example in [examples.boxblur_cumsum]:
        for input_image0 in [in_grayscale, in_color]:
            for dtype in [UInt(8), UInt(16), UInt(32), Float(32), Float(64)]:
                input_image = input_image0
#            for dtype in [UInt(16)]:
#            for dtype in [UInt(8), UInt(16)]:
                if example is examples.boxblur_sat or example is examples.boxblur_cumsum:
                    if dtype == UInt(32):
                        continue
                if example is examples.local_laplacian:
                    if input_image == in_color and (dtype == UInt(8) or dtype == UInt(16)):
                        pass
                    else:
                        continue
                if example is examples.snake:
                    if dtype != UInt(8) or input_image != in_color:
                        continue
                #print dtype.isFloat(), dtype.bits
                if example is examples.interpolate:
                    if not dtype.isFloat() or input_image != in_color:
                        continue
                    input_image = 'interpolate_in.png'
        #        (in_func, out_func) = examples.blur_color(dtype)
    #            (in_func, out_func) = examples.blur(dtype)
                (in_func, out_func, eval_func) = example(dtype)
                if first:
                    first = False
                    names.append((example_name, sorted(all_funcs(out_func).keys())))
        #        print 'got func'
        #        outf = filter_image(in_func, out_func, in_filename, dtype)
        #        print 'got filter'
                if do_filter:
                    outf = filter_image(in_func, out_func, input_image, disp_time=True, eval_func=eval_func)
                    out = outf()
                    out.show()
                    A = numpy.asarray(out)
        #        print 'shown'
#                print numpy.min(A.flatten()), numpy.max(A.flatten())
        #        out.save('out.png')
    print
    print 'Function names:'
    for (example_name, func_names) in names:
        print example_name, func_names

def test_all_funcs():
    f = Func('f_all_funcs')
    g = Func('g_all_funcs')
    h = Func('h_all_funcs')
    
    x,y = Var(),Var()
    h[x,y] = x**2+y**2
    g[x,y] = h[x,y]*2
    f[x,y] = g[x,y]+1
    assert sorted(all_funcs(f).keys()) == ['f_all_funcs', 'g_all_funcs', 'h_all_funcs']
    print 'test_all_funcs:   OK'

def test_numpy():
    def dist(a,b):
        a = numpy.asarray(a, 'float64') - numpy.asarray(b, 'float64')
        a = a.flatten()
        return numpy.mean(numpy.abs(a))

    for dtype in [UInt(8), UInt(16), UInt(32), Float(32), Float(64)]: #['int8', 'int16', 'int32', 'uint8', 'uint16', 'uint32', 'float32', 'float64']:
        for mul in [0, 1]:
#            a = numpy.asarray(PIL.open('lena.png'),dtype)*mul #numpy.array([[1,2],[3,4]],'float32')
            a = numpy.asarray(Image(dtype, 'lena.png'))*mul #numpy.array([[1,2],[3,4]],'float32')
            b = Image(a)
            c = numpy.asarray(b)
            assert a.dtype == c.dtype
            assert dist(a,c) < 1e-8

            if dtype == UInt(16):
                locals_d = test_func(in_image=a)
                test = locals_d['test']
                blur_y = locals_d['blur_y']
                out = locals_d['out']
                test(blur_y)
                #out[0].show()
                c = numpy.asarray(out[0])
                #print 'anorm:', dist(a,a*0), 'cnorm:', dist(c,c*0), numpy.min(a.flatten()), numpy.max(a.flatten()), numpy.min(c.flatten()), numpy.max(c.flatten()), a.dtype, c.dtype
                assert dist(a,c)<=1000

    print 'numpy: OK'
    
def test():
    exit_on_signal()
#    print 'a'
#    uint16 = UInt(16)
#    print 'b'
#    input = UniformImage(uint16, 3, 'input')
#    print 'c'
#    pass

    test_numpy()
    test_blur()
    test_all_funcs()
    test_core()
    test_segfault()
    test_examples()
#    test_examples()
#    test_autotune()
    print
    print 'All tests passed, done'

#exit_on_signal()
    
if __name__ == '__main__':
    test()
    
