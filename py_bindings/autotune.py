
# Jonathan+Andrew discussion:
# - Tile size or unroll size needs to be divisible into input size
# - split(x, x, xi, n) is right syntax for now
# - Only vectorize at native vector widths, 2 4 8 16
# - tile() should be in the same order as the variables
# - Should not be 2 vectorize calls
# - Good to have a clamp at the beginning and a dummy function at the end -- autotuner maybe should inject these (optionally), at least the dummy at the end.
   # Could tune over parameters have I clamped at the beginning and have I injected dummy at the end.
   # Smartest thing
#
#  x should be outside y, which is f(y,x,c)
#  transpose(x, y) means push x just far enough to be inside y
#    If split to introduce new variables one becomes two in the same spot. Same with tile, ends up in the order (a,b,c,d) for tile(a,b,c,d).
#  
#  f(y, ..., x)
#  f.transpose(y,x)
#  --> f(...,x,y)
#  foreach y:
#    foreach x:
#        foreach ...:
#permute([defined order list], [desired order list]):
#
# ------------------------------------------------------------------
#
# Autotuner (not using Petabricks)

# Randomly generate size 3

# Connelly TODO research:
# - enumerate valid schedules
# - machine learning schedule at a given point
# - dynamic programming or tree search or iterative algorithm to find initial guess / plausible schedules

# TODO:
# - global creation (for more than one function)
#   - chunk (and all functions actually) should list variables that are created due to split/tile (see e.g. camera_raw).
# - create random schedule as well as enumerate all schedules of a given length
# - transpose -- does it always need to be at the end?

import halide
import random
import collections
import itertools
import permutation
import time
import copy
import numpy
import sys
import os
random_module = random

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
    def fragments(root_func, func, cls, vars, extra_caller_vars):
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
    def fragments(root_func, func, cls, vars, extra_caller_vars):
#        print 'fragments', cls
        return [cls(x) for x in vars]

use_random_blocksize = True

def blocksize_random():
    return random.choice([2,4,8,16,32]) if use_random_blocksize else 3

class FragmentBlocksizeMixin(FragmentVarMixin):
    def __init__(self, var=None, value=None):
#        print '__init__', self.__class__
        self.var = var
        self.value = value
        if self.value is None:
            self.value = 0

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
        s = str(x)
        if s in d:
            return False
        d.add(s)
        
    return True

class FragmentRoot(Fragment):
    def __str__(self):
        return '.root()'

class FragmentVectorize(FragmentBlocksizeMixin,Fragment):
    def __str__(self):
        return '.vectorize(%s,%d)'%(self.var, 4) #self.value) # FIXMEFIXME Generate random platform valid blocksize
    
class FragmentParallel(FragmentVarMixin,Fragment):
    def __str__(self):
        return '.parallel(%s)'%(self.var)

class FragmentUnroll(FragmentBlocksizeMixin,Fragment):
    def __str__(self):
        return '.unroll(%s,%d)'%(self.var,self.value)

class FragmentChunk(Fragment):
    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        return [cls(x) for x in caller_vars(root_func, func)+extra_caller_vars]
        
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

def instantiate_var(name, cache={}):
    if name in cache:
        return cache[name]
    cache[name] = halide.Var(name)
    return cache[name]

# split(x, x, xi, n)
class FragmentSplit(FragmentBlocksizeMixin,Fragment):
    def __init__(self, var=None, value=None, newvar=None, reuse_outer=False,vars=None):
        FragmentBlocksizeMixin.__init__(self, var, value)
        self.newvar = newvar
        if self.newvar is None:
            self.newvar = create_var(vars)
        self.reuse_outer = reuse_outer
        
    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        return [cls(x,reuse_outer=True,vars=vars)  for x in vars]
        #([cls(x,reuse_outer=False,vars=vars) for x in vars] +
        #        [cls(x,reuse_outer=True,vars=vars)  for x in vars])

    def new_vars(self):
        return [self.newvar]
    
    def __str__(self):
        return '.split(%s,%s,%s,%d)'%(self.var,self.var if     self.reuse_outer else self.newvar,
                                               self.var if not self.reuse_outer else self.newvar, self.value)

class FragmentTile(FragmentBlocksizeMixin,Fragment):
    def __init__(self, xvar=None, yvar=None, newvar=None, vars=None):
        self.xvar=xvar
        self.yvar=yvar
        self.xsize = 0
        self.ysize = 0
        self.xnewvar = create_var(vars)
        self.ynewvar = create_var(vars+[self.xnewvar])

    def randomize(self):
        self.xsize = blocksize_random()
        self.ysize = blocksize_random()

    def check(self, L):
        return check_duplicates(self.__class__, L)

    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        ans = []
        for i in range(len(vars)):
            for j in range(i+1, len(vars)):
                ans.append(cls(vars[i],vars[j],vars=vars))
        return ans
#        return [cls(x,y,vars=vars) for x in vars for y in vars if x != y]

    def new_vars(self):
        return [self.xnewvar, self.ynewvar]
    
    def __str__(self):
        return '.tile(%s,%s,%s,%s,%d,%d)'%(self.xvar,self.yvar,self.xnewvar,self.ynewvar,self.xsize,self.ysize)

class FragmentTranspose(Fragment):
    # Actually makes potentially many calls to transpose, but is considered as one fragment
    def __init__(self, vars, idx):
        self.vars = vars
        self.idx = idx
        self.permutation = permutation.permutation(vars, idx)
        self.pairwise_swaps = permutation.pairwise_swaps(vars, self.permutation)
    
    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        return [cls(vars=vars, idx=i) for i in range(1,permutation.factorial(len(vars)))]     # TODO: Allow random generation so as to not loop over n!
    
    def check(self, L):
        if not default_check(self.__class__, L):
            return False
        return [isinstance(x, FragmentTranspose) for x in L] == [0]*(len(L)-1)+[1]
    
    def __str__(self):
        ans = ''
        assert len(self.pairwise_swaps) >= 1
        for (a, b) in self.pairwise_swaps:
            ans += '.transpose(%s,%s)'%(a,b)
        return ans

fragment_classes = [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentSplit, FragmentTile, FragmentTranspose]

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

    def new_vars(self):
        ans = []
        for x in self:
            ans.extend(x.new_vars())
        return list(sorted(set(ans)))
        
    def __repr__(self):
        return str(self) #return 'FragmentList(%s, %r)' % (self.func, repr([str(x) for x in list(self)]))
        
    def randomized(self):
        ans = FragmentList(self.func, [copy.copy(x) for x in self])
        for x in ans:
            x.randomize()
        return ans
    
    def check(self):
        for x in self:
            if not x.check(self):
                return False
        return True

def schedules_depth(root_func, func, vars, depth=0, random=False, extra_caller_vars=[]):
    "Un-checked schedules of exactly the specified depth for the given function."
#    print func
#    print vars
    if not random:
        randomized = lambda x: x
    else:
        def randomized(La):
            ans = list(La)
            random_module.shuffle(ans)
            return ans
    assert depth >= 0 and isinstance(depth, (int,long))
    
    if depth == 0:
        yield FragmentList(func, [])
    else:
        for L in schedules_depth(root_func, func, vars, depth-1, random):
            if not L.check():
                continue
            all_vars = list(vars)
            for fragment in L:
                all_vars.extend(fragment.new_vars())
            for cls in randomized(fragment_classes):
                #print 'all_vars', all_vars
                for fragment in randomized(cls.fragments(root_func, func, cls, all_vars, extra_caller_vars)):
                    #print 'fragment', fragment
                #print '=>', fragment
                    #print '*', len(L), L
                    yield FragmentList(func, list(L) + [fragment])

DEFAULT_MAX_DEPTH = 3

def schedules_func(root_func, func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, random=False, extra_caller_vars=[], vars=None):
    """
    Generator of valid schedules for a function, each of which is a list of schedule fragments (FragmentList).
    
    If random is True then instead generate exactly one schedule randomly chosen.
    """
    if vars is None:
        vars = halide.func_varlist(func)    
    #if func.name() == 'f':
    #    yield FragmentList(func, [random_module.choice(FragmentChunk.fragments(root_func, func, FragmentChunk, vars, extra_caller_vars))])
    #    return
    for depth in range(min_depth, max_depth+1):
        if random:
            depth = random_module.randrange(min_depth, max_depth+1)
        for L in schedules_depth(root_func, func, vars, depth, random, extra_caller_vars):
            if L.check():
                yield L.randomized()
                if random:
                    return

class Schedule:
    def __init__(self, root_func, d):
        self.root_func = root_func
        self.d = d
    
    def __str__(self):
        d = self.d
        ans = []
        for key in sorted(d.keys()):
            s = str(d[key])
            if s != '':
                ans.append(s)
        return '\n'.join(ans) #join(['-'*40] + ans + ['-'*40])

    def new_vars(self):
        ans = []
        for x in self.d.values():
            ans.extend(x.new_vars())
        return list(sorted(set(ans)))

    def apply(self):   # Apply schedule
        #print 'apply schedule:'
        #print str(self)
        halide.inline_all(self.root_func)
        scope = halide.all_vars(self.root_func)
        #print 'scope', scope.keys()
        new_vars = self.new_vars()
        #print 'new_vars', new_vars
        for varname in new_vars:
            scope[varname] = instantiate_var(varname)
        print 'scope:', scope
        def callback(f, parent):
            name = f.name()
            if name in self.d:
                s = str(self.d[name])
                s = s.replace(name + '.', '__func.')
                scope['__func'] = f
                exec s in scope
        halide.visit_funcs(self.root_func, callback)
    
    def test(self, shape, input, eval_func=None):
        """
        Test on zero array of the given shape. Return evaluate() function which when called returns the output Image.
        """
        print 'apply'
        self.apply()
        print 'in_image'
        in_image = numpy.zeros(shape, input.type().to_numpy())
        print 'filter'
        return halide.filter_image(input, self.root_func, in_image, eval_func=eval_func)
        
def random_schedule(root_func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, vars=None):
    """
    Generate Schedule for all functions called by root_func (recursively). Same arguments as schedules_func().
    """
    if vars is None:
        vars = halide.func_varlist(root_func)
    d_new_vars = {}
    schedule = {}
    def callback(f, parent):
        extra_caller_vars = d_new_vars.get(parent.name() if parent is not None else None,[])
#        print 'schedule', f.name(), extra_caller_vars
#        ans = schedules_func(root_func, f, min_depth, max_depth, random=True, extra_caller_vars=extra_caller_vars, vars=vars).next()
        max_depth_sel = max_depth if f.name() != 'f' else 0
        ans = schedules_func(root_func, f, min_depth, max_depth_sel, random=True, extra_caller_vars=extra_caller_vars).next()
        d_new_vars[f.name()] = ans.new_vars()
        schedule[f.name()] = ans
        
    halide.visit_funcs(root_func, callback)
    return Schedule(root_func, schedule)

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
    
def test_schedules(verbose=False, test_random=False):
    #random_module.seed(int(sys.argv[1]) if len(sys.argv)>1 else 0)
    halide.exit_on_signal()
    f = halide.Func('f')
    x = halide.Var('x')
    y = halide.Var('y')
    c = halide.Var('c')
    g = halide.Func('g')
    v = halide.Var('v')
    input = halide.UniformImage(halide.UInt(16), 3)
    int_t = halide.Int(32)
    f[x,y,c] = input[halide.clamp(x,halide.cast(int_t,0),halide.cast(int_t,input.width()-1)),
                     halide.clamp(y,halide.cast(int_t,0),halide.cast(int_t,input.height()-1)),
                     halide.clamp(c,halide.cast(int_t,0),halide.cast(int_t,2))]
    #g[v] = f[v,v]
    g[x,y,c] = f[x,y,c]+1
    assert sorted(halide.all_vars(g).keys()) == sorted(['x', 'y', 'c']) #, 'v'])

    if verbose:
        print halide.func_varlist(f)
        print 'caller_vars(f) =', caller_vars(g, f)
        print 'caller_vars(g) =', caller_vars(g, g)
    
#    validL = list(valid_schedules(g, f, 4))
#    validL = [repr(_x) for _x in validL]
#    
#    for L in sorted(validL):
#        print repr(L)
    T0 = time.time()
    if not test_random:
        random = True #False
        nvalid_determ = 0
        for L in schedules_func(g, f, 0, 3):
            nvalid_determ += 1
            if verbose:
                print L
        nvalid_random = 0
        for i in range(100):
            for L in schedules_func(g, f, 0, DEFAULT_MAX_DEPTH, random=True): #sorted([repr(_x) for _x in valid_schedules(g, f, 3)]):
                if verbose and 0:
                    print L#repr(L)
                nvalid_random += 1
    s = []
    for i in range(400):
        d = random_schedule(g, 0, DEFAULT_MAX_DEPTH)
        si = str(d)
        s.append(si)
        if verbose:
            print 'Schedule:', si

        d.apply()
        evaluate = d.test((36, 36, 3), input)
        print 'evaluate'
        evaluate()
        if test_random:
            print 'Success'
            sys.exit()
    T1 = time.time()
    
    s = '\n'.join(s)
    assert 'f.chunk(_c0)' in s
    assert 'f.root().vectorize' in s
    assert 'f.root().unroll' in s
    assert 'f.root().split' in s
    assert 'f.root().tile' in s
    assert 'f.root().parallel' in s
    assert 'f.root().transpose' in s

    assert nvalid_random == 100
    if verbose:
        print 'generated in %.3f secs' % (T1-T0)
    print 'random_schedule: OK'
    
def main():
    args = sys.argv[1:]
    if len(args) == 0:
        print 'autotune test|print'
        sys.exit(0)
    if args[0] == 'test':
        test_schedules(True)
    if args[0] == 'test_random':
        global use_random_blocksize
        use_random_blocksize = False
        test_schedules(True, test_random=True)
    elif args[0] == 'print':
        nprint = 10
        if len(args) > 1:
            nprint = int(args[1])
        cache = set()
        for i in range(nprint):
            if os.path.exists('out.txt'):
                os.remove('out.txt')
            os.system('python autotune.py test_random > out.txt')
            s = open('out.txt', 'rt').read()
            success = 'Success' in s
            try:
                j = s.index('Schedule:')
            except:
                continue
            schedule = s[j:s.index('\n',j+1)]
            if schedule not in cache:
                print 'Success' if success else 'Failed ', schedule
                sys.stdout.flush()
                cache.add(schedule)
#    test_schedules()
    
if __name__ == '__main__':
    main()
    