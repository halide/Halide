
# Autotuner (not using Petabricks)

# Connelly TODO research:
# - enumerate valid schedules
# - machine learning schedule at a given point
# - dynamic programming or tree search or iterative algorithm to find initial guess / plausible schedules

# TODO:
# - chunk (and all functions actually) should list variables that are created due to split/tile (see e.g. camera_raw).
# - global creation (for more than one function)
# - create random schedule as well as enumerate all schedules of a given length

import halide
import random
import collections
import itertools

FUNC_ROOT   = 0
FUNC_INLINE = 1
FUNC_CHUNK  = 2         # Needs a variable in the caller
# Chunk not implemented yet

VAR_SERIAL             = 0
VAR_VECTORIZE          = 1
VAR_PARALLEL           = 2
VAR_UNROLL             = 3
VAR_TILE               = 4
VAR_SPLIT              = 5
# Tile and split not implemented yet (recursion not implemented). Also vectorize() and unroll() implicitly create a new variable so they recurse also.
# transpose() always there or sometimes not there
# To get long schedules should be able to randomize from an existing schedule.
# Also schedules have some global interactions when new variables are introduced so refactor to handle that.

def default_check(cls, L):
    def count(C):
        return sum([isinstance(x, C) for x in L])
    if len(L) == 0:
        return True
    else:
        # Handle singleton fragments
        if isinstance(L[0], FragmentRoot) and count(FragmentRoot) == 1 and count(FragmentChunk) == 0:
            return True
        elif isinstance(L[0], FragmentChunk) and len(L) == 1:
            return True
    return False
            
class Fragment:
    "Base class for schedule fragment e.g. .vectorize(x), .parallel(y), .root(), etc."
    def __init__(self, var=None, value=None):
        self.var = var
        self.value = value
        
    @staticmethod
    def fragments(root_func, func, cls, vars):
        "Given class and variable list (of strings) returns fragments possible at this point."
#        print 'fragments base', cls
        return [cls()]
        
    def ___str__(self):
        "Returns schedule_str, e.g. '.parallel(y)'."
    
    def new_vars(self):
        "List of new variable names, e.g. ['v'] or []."
        return []
        
    def randomize(self):
        "Randomize values e.g. change vectorize(x, 8) => vectorize(x, (random value))."
    
    def check(self, L):
        "Given list of Schedule fragments (applied to a function) returns True if valid else False."
        return default_check(self.__class__, L)

class FragmentVarMixin:
    @staticmethod
    def fragments(root_func, func, cls, vars):
#        print 'fragments', cls
        return [cls(x) for x in vars]

def blocksize_random():
    return random.choice([2,4,8,16,32])

class FragmentBlocksizeMixin(FragmentVarMixin):
    def __init__(self, var=None, value=None):
#        print '__init__', self.__class__
        self.var = var
        self.value = value
        if self.value is None:
            self.randomize()

    def randomize(self):
        self.value = blocksize_random()

    def check(self, L):
        return check_duplicates(self.__class__, L)

def check_duplicates(cls, L):
    if not default_check(cls, L):
        return False
    #count = collections.defaultdict(lambda: 0)
    #for x in L:
    #    if isinstance(x, cls):
    #        count[x.var] += 1
    #        if count[x.var] >= 2:
    #            return False
    d = set()
    for x in L:
        s = repr(x)
        if s in d:
            return False
        d.add(s)
        
    return True

class FragmentRoot(Fragment):
    def __str__(self):
        return '.root()'

class FragmentVectorize(FragmentBlocksizeMixin,Fragment):
    def __str__(self):
        return '.vectorize(%s,%d)'%(self.var, self.value)
    
class FragmentParallel(FragmentBlocksizeMixin,Fragment):
    def __str__(self):
        return '.parallel(%s,%d)'%(self.var,self.value)

class FragmentUnroll(FragmentBlocksizeMixin,Fragment):
    def __str__(self):
        return '.unroll(%s,%d)'%(self.var,self.value)

class FragmentChunk(Fragment):
    @staticmethod
    def fragments(root_func, func, cls, vars):
        return [cls(x) for x in caller_vars(root_func, func)]
        
    def check(self, L):
        return check_duplicates(self.__class__, L)

    def __str__(self):
        return '.chunk(%s)'%self.var

def create_var(vars): #count=[0]):
    #count[0] += 1
    for i in itertools.count(0):
        s = '_c%d'%i#count[0]
        if not s in vars:
            return s

class FragmentSplit(FragmentBlocksizeMixin,Fragment):
    def __init__(self, var=None, value=None, newvar=None, reuse_outer=False,vars=None):
        FragmentBlocksizeMixin.__init__(self, var, value)
        self.newvar = newvar
        if self.newvar is None:
            self.newvar = create_var(vars)
        self.reuse_outer = reuse_outer
        
    @staticmethod
    def fragments(root_func, func, cls, vars):
        return ([cls(x,reuse_outer=False,vars=vars) for x in vars] +
                [cls(x,reuse_outer=True,vars=vars)  for x in vars])

    def new_vars(self):
        return [self.newvar]
    
    def __str__(self):
        return '.split(%s,%s,%s,%d)'%(self.var,self.var if     self.reuse_outer else self.newvar,
                                               self.var if not self.reuse_outer else self.newvar, self.value)

class FragmentTile(FragmentBlocksizeMixin,Fragment):
    def __init__(self, xvar=None, yvar=None, newvar=None, vars=None):
        self.xvar=xvar
        self.yvar=yvar
        self.randomize()
        self.xnewvar = create_var(vars)
        self.ynewvar = create_var(vars+[self.xnewvar])

    def randomize(self):
        self.xsize = blocksize_random()
        self.ysize = blocksize_random()

    def check(self, L):
        return check_duplicates(self.__class__, L)

    @staticmethod
    def fragments(root_func, func, cls, vars):
        return [cls(x,y,vars=vars) for x in vars for y in vars if x != y]

    def new_vars(self):
        return [self.xnewvar, self.ynewvar]
    
    def __str__(self):
        return '.tile(%s,%s,%s,%s,%d,%d)'%(self.xvar,self.yvar,self.xnewvar,self.ynewvar,self.xsize,self.ysize)

fragment_classes = [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentSplit, FragmentTile]

class FragmentList(list):
    def __init__(self, func, L):
        self.func = func
        list.__init__(self, L)
        
    def __str__(self):
        #print '__str__', list(self)
        ans = []
        for x in self:
            #print 'str of', x
            #print 'next'
            ans.append(str(x))
        if len(ans):
            #print 'returning list'
            return self.func.name() + ''.join(ans)
        #print 'returning empty'
        return ''

    def __repr__(self):
        return str(self) #return 'FragmentList(%s, %r)' % (self.func, repr([str(x) for x in list(self)]))
        
    def randomize(self):
        for x in self:
            x.randomize()
    
def schedules_depth(root_func, func, vars, depth=0):
    "Un-checked schedules of exactly the specified depth for the given function."
#    print func
#    print vars
    assert depth >= 0 and isinstance(depth, (int,long))
    
    if depth == 0:
        yield FragmentList(func, [])
    else:
        for cls in fragment_classes:
            for L in schedules_depth(root_func, func, vars, depth-1):
                all_vars = list(vars)
                for fragment in L:
                    all_vars.extend(fragment.new_vars())
                #print 'all_vars', all_vars
                for fragment in cls.fragments(root_func, func, cls, all_vars):
                    #print 'fragment', fragment
                #print '=>', fragment
                    #print '*', len(L), L
                    yield FragmentList(func, list(L) + [fragment])

def valid_schedules(root_func, func, max_depth=4):
    "A sequence of valid schedules for a function, each of which is a list of schedule fragments (up to a maximum depth)."
    vars = halide.func_varlist(func)    
    for depth in range(max_depth+1):
        for L in schedules_depth(root_func, func, vars, depth):
            ok = True
            for x in L:
                #print 'at depth=%d, checking'%depth, str(L)#, len(L)
                if not x.check(L):
                    #print 'check failed'
                    ok = False
                    break
            if ok:
                yield L

def func_lhs_var_names(f):
    ans = []
    for y in f.args():
        for x in y.vars():
            ans.append(x.name())
    return ans
    
def caller_vars(root_func, func):
    "Given a root Func and current function return list of variables of the caller."
    func_name = func.name()
    for (name, g) in halide.all_funcs(root_func).items():
        rhs_names = [x.name() for x in g.rhs().funcs()]
        if func_name in rhs_names:
            return func_lhs_var_names(g)
    return []
    
def test_schedules():
    f = halide.Func('f')
    x = halide.Var('x')
    y = halide.Var('y')
    g = halide.Func('g')
    v = halide.Var('v')
    f[x,y] = 1
    g[v] = f[v,v]

    print halide.func_varlist(f)
    print 'caller_vars(f) =', caller_vars(g, f)
    print 'caller_vars(g) =', caller_vars(g, g)
    
    validL = list(valid_schedules(g, f, 3))
    
    for L in validL:
        print repr(repr(L))

    print 'number valid: ', len(validL)
    
if __name__ == '__main__':
    test_schedules()
    