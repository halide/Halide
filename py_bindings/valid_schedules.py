
# Ordinary mode: Reorder is last
# CUDA mode:     Last two are Reorder().CudaTile()
# Use gpuChunk() for chunking against the blockidx variable.

import halide
import random
import copy
import permutation
import numpy
import itertools
import sys
import os
import parseutil
random_module = random

DEFAULT_MAX_DEPTH = 4
FORCE_TILE = False
MUTATE_TRIES = 10
TILE_PROB_SQUARE = 0.5              # Probability of selecting square tile size (e.g. 8x8).
SPLIT_STORE_COMPUTE = True          # Whether to use chunk(store, compute)
CHUNK_ROOT = True                   # Whether to allow chunk(root, compute)
MAXLEN = 300                        # Maximum length in display of schedule through oneline()

CHECK_VERBOSE = False
CHUNK_VARS_VERBOSE = False
VAR_ORDER_VERBOSE = False

root_var = 'root'                   # Turns into halide.root...special variable for chunk(root, compute)

CUDA_CHUNK_VAR = 'blockidx'

_cuda_mode = [False]                # Global variable for whether cuda mode. TODO: Remove global variable.
def set_cuda(cuda):
#    print 'set_cuda', cuda
    _cuda_mode[0] = cuda
def is_cuda():
    return _cuda_mode[0]
    
# --------------------------------------------------------------------------------------------------------------
# Valid Schedule Enumeration
# --------------------------------------------------------------------------------------------------------------

def default_check(cls, L, func):
    assert func is not None
    if halide.update_func_parent(func) is not None:     # If we are scheduling an f.update()
        if len(L) < 1 or not isinstance(L[0], FragmentRoot):
            return False

    def count(C):
        return sum([isinstance(x, C) for x in L])
    if len(L) == 0:
        #if func.isReduction():
        #    return False
        return True         # Inline
    else:
        # Handle singleton fragments
        if count(FragmentVectorize) > 1:        # Allow at most one vectorize per Func schedule (FragmentList)
            if CHECK_VERBOSE:
                print '  * check ', cls, 'vectorize fail'
            return False
        if FORCE_TILE and (len(L) >= 2 and count(FragmentTile) < 1):
            if CHECK_VERBOSE:
                print '  * check ', cls, 'tile fail'
            return False
        root_count = count(FragmentRoot)
        chunk_count = count(FragmentChunk)
        if isinstance(L[0], FragmentRoot) and root_count == 1 and chunk_count == 0:
            return True
        elif isinstance(L[0], FragmentChunk) and chunk_count == 1 and root_count == 0:
            return True
    if CHECK_VERBOSE:
        print '  * check ', cls, 'generic fail'
    return False
            
def make_fromstring(cls):
    @staticmethod
    def fromstring(var=None, value=None, func=None):
        return cls(var, int(value) if value is not None else value)
    return fromstring

class Fragment:
    "Base class for a single schedule macro applied to a Func, e.g. .vectorize(x), .parallel(y), .root(), etc."
    def __init__(self, var=None, value=None):
        self.var = var
        self.value = value
        
    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        "Given class and variable list (of strings) returns a random fragment possible at this point or None if none is possible."
#        print 'fragments base', cls
        return cls()
        
    def ___str__(self):
        "Returns schedule_str, e.g. '.parallel(y)'."
    
    def new_vars(self):
        "List of new variable names, e.g. ['v'] or []."
        return []
        
    def randomize_const(self):
        "Randomize constants e.g. change vectorize(x, 8) => vectorize(x, (random value))."
    
    def check(self, L, partial_schedule=None, func=None, vars=None):
        "Given list of Schedule fragments (applied to a function) returns True if valid else False."
        return default_check(self.__class__, L, func)

    def var_order(self, prev_order):
        """
        Given var (loop) order (list of variable string names) from previous Fragment (or initial var order), get order after this Fragment.
        
        If f(x,y,c) then the loop ordering is [c y x] -- the reverse of argument ordering.
        """
        return prev_order
        
class FragmentVarMixin:
    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
#        print 'fragments', cls
        return cls(random.choice(vars)) if len(vars) else None #[cls(x) for x in vars]

    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None and self.var not in vars:
            if CHECK_VERBOSE:
                print ' * check, var fail %s, %r' % (self.var, vars)
            return False
        return default_check(self.__class__, L, func)

use_random_blocksize = True

def blocksize_random(L=None, force_random=False):
    if L is None:
        L = [2,4,8,16,32,64]
    return random.choice(L) #if (use_random_blocksize and not force_random) else 3

class FragmentBlocksizeMixin(FragmentVarMixin):
    def __init__(self, var=None, value=None):
#        print '__init__', self.__class__
        self.var = var
        self.value = value
        if self.value is None:
            self.randomize_const()

    def randomize_const(self):
        self.value = blocksize_random()
        #print 'randomize_const, value=%d'% self.value

    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None and self.var not in vars:
            if CHECK_VERBOSE:
                print ' * check BlocksizeMixin var %s fails %r' % (self.var, vars)
            return False
        return check_duplicates(self.__class__, L, func)

def check_duplicates(cls, L, func):
    if not default_check(cls, L, func):
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
            if CHECK_VERBOSE:
                print '* check_duplicates', cls, 'fail', s
            return False
        d.add(s)
        
    return True

class FragmentRoot(Fragment):
    def __str__(self):
        return '.root()'
    
    def var_order(self, prev_order):
        raise ValueError('var_order called on FragmentRoot()')
        
class FragmentVectorize(FragmentBlocksizeMixin,Fragment):
    def randomize_const(self):
        self.value = blocksize_random([2,4,8,16])

    def __str__(self):
        return '.vectorize(%s,%d)'%(self.var, self.value) #self.value) # FIXMEFIXME Generate random platform valid blocksize

class FragmentParallel(FragmentVarMixin,Fragment):
    def __str__(self):
        return '.parallel(%s)'%(self.var)

class FragmentUnroll(FragmentBlocksizeMixin,Fragment):
    def randomize_const(self):
        self.value = blocksize_random([2,3,4])

    def __str__(self):
        return '.unroll(%s,%d)'%(self.var,self.value)

class FragmentChunk(Fragment):
    def __init__(self, var=None, storevar=None, func=None):
        self.var = var
        self.storevar = storevar
        self.func = func
        assert self.func is not None

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        #allV = caller_vars(root_func, func)+extra_caller_vars
        allV = list(reversed(chunk_vars(partial_schedule, func)))           # In loop order
        if len(allV) == 0:
            return None
        if SPLIT_STORE_COMPUTE:
            if is_cuda() and CUDA_CHUNK_VAR in allV:          # Cannot chunk anything that is CUDA variable on in
                allV = allV[:allV.index(CUDA_CHUNK_VAR)+1]
            n = len(allV)
            if CHUNK_ROOT:
                n += 1
            j = random.randrange(len(allV))           # Compute. TODO: Optionally use exponential distribution instead of uniform
            if allV[j] == CUDA_CHUNK_VAR:
                if len(halide.func_varlist(func)) < 2:
                    return None                         # Invalid since cudaChunk(CUDA_CHUNK_VAR, vars[0], vars[1]) will fail
            #    return cls(allV[j], allV[j], func=func)
            i = random.randrange(-1, j+1)             # Store
            assert j >= i
            #fsplitstore = open('splitstore.txt', 'at')
            #print >>fsplitstore, 'split_store_compute chunk selecting %r %d %d %s %s' % (allV, i, j, allV[i], allV[j])
            #fsplitstore.close()
            return cls(allV[j], allV[i] if i >= 0 else root_var, func)
        else:
            return cls(random.choice(allV), func=func)
        #return [cls(x) for x in ]
        
    def check(self, L, partial_schedule=None, func=None, vars=None):
        if partial_schedule is not None:
            cvars = list(reversed(chunk_vars(partial_schedule, func)))          # In loop ordering
            if self.var not in cvars:
                if CHECK_VERBOSE:
                    print ' * check fail 1, chunk compute=%s store=%s %r, func=%s' % (self.var, self.storevar, cvars, func.name())
                return False
            if SPLIT_STORE_COMPUTE:
                if is_cuda() and CUDA_CHUNK_VAR in cvars:          # Cannot chunk anything that is CUDA variable on in
                    if self.var in cvars[cvars.index(CUDA_CHUNK_VAR)+1:]:
                        if CHECK_VERBOSE:
                            print ' * check fail 4, chunk compute=%s store=%s %r' % (self.var, self.storevar, cvars)
                        return False
                if CHUNK_ROOT and self.storevar == root_var:
                    pass
                elif self.var == CUDA_CHUNK_VAR:
                    pass
                else:
                    if self.storevar not in cvars:
                        if CHECK_VERBOSE:
                            print ' * check fail 2, chunk compute=%s store=%s %r' % (self.var, self.storevar, cvars)
                        return False
                    j = cvars.index(self.var)           # Compute
                    i = cvars.index(self.storevar)      # Store
                    #if CHECK_VERBOSE:
                    #    print ' * check: cvars=%r, i=%d, j=%d' % (cvars, i, j)
                    if j < i:           # If compute is outside store (meaning index is less) then raise error
                        if CHECK_VERBOSE:
                            print ' * check fail 3, chunk compute var=%s, store var=%s, i=%d, j=%d, wrong order, cvars=%r' % (self.var, self.storevar, i, j, cvars)
                        return False
        return check_duplicates(self.__class__, L, func)

    def __str__(self):
        # Only switch to cudaChunk if compute variable is blockidx
        if self.var == CUDA_CHUNK_VAR:
            vars = halide.func_varlist(self.func)
            if len(vars) >= 2:
                return '.cudaChunk(%s,%s,%s,%s)' % (self.storevar,self.var,vars[0],vars[1])      # TODO: Make this work in general case...need to know elided dimensions
            else:
                raise ValueError
        if SPLIT_STORE_COMPUTE:
            return '.chunk(%s,%s)'%(self.storevar, self.var)
        else:
            return '.chunk(%s)'%self.var

    @staticmethod
    def fromstring(avar=None, bvar=None, vars0=None, vars1=None, func=None):
        if SPLIT_STORE_COMPUTE:
            if bvar is None:
                bvar = avar
            return FragmentChunk(bvar, avar, func=func)
        else:
            assert bvar is None
            return FragmentChunk(avar, func=func)

    def var_order(self, prev_order):
        raise ValueError('var_order called on FragmentChunk()')

# FragmentUpdate is just a stub class for now -- not used in tuning, just for comparing with human reference schedules
class FragmentUpdate(Fragment):
    def __str__(self):
        return '.update()'

    def var_order(self, prev_order):
        raise ValueError('var_order called on FragmentUpdate()')

# FragmentBound is just a stub class for now -- not used in tuning, just for comparing with human reference schedules
class FragmentBound(Fragment, FragmentBlocksizeMixin):
    def __init__(self, var=None, lower=None, size=None):
#        print '__init__', self.__class__
        self.var = var
        self.lower = lower
        self.size = size

    def __str__(self):
        return '.bound(%s,%d,%d)'%(self.var,self.lower,self.size)

    def var_order(self, prev_order):
        return list(prev_order)

    @staticmethod
    def fromstring(var, lower, size, func=None):
        return FragmentBound(var, int(lower), int(size))

for _cls in [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentUpdate]:
    _cls.fromstring = make_fromstring(_cls)

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
    def __init__(self, var=None, value=None, newvar=None, reuse_outer=True,vars=None):
        FragmentBlocksizeMixin.__init__(self, var, value)
        self.newvar = newvar
        if self.newvar is None:
            self.newvar = create_var(vars)
        self.reuse_outer = reuse_outer
        assert self.reuse_outer

    @staticmethod
    def fromstring(var=None, var_repeat=None, newvar=None, value=None, func=None):
        return FragmentSplit(var, int(value) if value is not None else value, newvar)

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        #return [cls(x,reuse_outer=True,vars=vars)  for x in vars]
        return cls(random.choice(vars),reuse_outer=True,vars=vars) if len(vars) else None
        #([cls(x,reuse_outer=False,vars=vars) for x in vars] +
        #        [cls(x,reuse_outer=True,vars=vars)  for x in vars])

    def new_vars(self):
        return [self.newvar]
    
    def __str__(self):
        return '.split(%s,%s,%s,%d)'%(self.var,self.var if     self.reuse_outer else self.newvar,
                                               self.var if not self.reuse_outer else self.newvar, self.value)

    def var_order(self, prev_order):            # if f(x,y,c)   after split(x,x,xi)     the loop order is   for c: for y: for x: for xi:
        try:
            i = prev_order.index(self.var)
        except ValueError:
            raise BadScheduleError
        return prev_order[:i] + [self.var, self.newvar] + prev_order[i+1:]

class FragmentTileBase(FragmentBlocksizeMixin,Fragment):
    def __init__(self, xvar=None, yvar=None, newvar=None, vars=None, xnewvar=None, ynewvar=None, xsize=None, ysize=None):
        self.xvar=xvar
        self.yvar=yvar
        self.xsize = 0 if xsize is None else xsize
        self.ysize = 0 if ysize is None else ysize
        self.xnewvar = create_var(vars)                if xnewvar is None else xnewvar
        self.ynewvar = create_var(vars+[self.xnewvar]) if ynewvar is None else ynewvar
        if self.xsize == 0 and self.ysize == 0:
            self.randomize_const()

    def randomize_const(self):
        if random.random() < TILE_PROB_SQUARE:
            self.xsize = self.ysize = blocksize_random()
        else:
            self.xsize = blocksize_random()
            self.ysize = blocksize_random()
        #print 'randomize_const, tile, size=%d,%d' % (self.xsize, self.ysize)

class FragmentTile(FragmentTileBase):
    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None and (self.xvar not in vars or self.yvar not in vars):
            if CHECK_VERBOSE:
                print ' * check fail tile %s %s %r' % (self.xvar, self.yvar, vars)
            return False
        return check_duplicates(self.__class__, L, func)

    def new_vars(self):
        return [self.xnewvar, self.ynewvar]

    @staticmethod
    def fromstring(xvar, yvar, xnewvar, ynewvar, xsize, ysize, func=None):
        return FragmentTile(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize), xnewvar=xnewvar, ynewvar=ynewvar)

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        if len(vars)-1 <= 0:
            return None
        i = random.randrange(len(vars)-1)
        return cls(vars[i+1],vars[i],vars=vars)
        #ans = []
        #for i in range(len(vars)-1):
        #    j = i+1
        #    #for j in range(i+1, len(vars)):
        #    ans.append(cls(vars[i],vars[j],vars=vars))
        #return ans
#        return [cls(x,y,vars=vars) for x in vars for y in vars if x != y]
    
    def __str__(self):
        return '.tile(%s,%s,%s,%s,%d,%d)'%(self.xvar,self.yvar,self.xnewvar,self.ynewvar,self.xsize,self.ysize)

    def var_order(self, prev_order):      # if f(x y c) after tile(x,y,xi,yi) the loop ordering is for c: for y: for x: for yi: for xi   [c y x yi xi]
                                          # previously the loop ordering was  for c: for y: for x     [c y x]
        #print 'prev_order', prev_order
        #print self.xvar, self.yvar
        try:
            i = prev_order.index(self.yvar)
        except ValueError:
            raise BadScheduleError
        if i+1 >= len(prev_order):
            raise BadScheduleError((i, self.xvar, prev_order))
        if self.xvar != prev_order[i+1]:
            raise BadScheduleError()
        ans = prev_order[:i] + [self.yvar, self.xvar, self.ynewvar, self.xnewvar] + prev_order[i+2:]
        assert len(ans) == len(prev_order) + 2
        return ans

class FragmentCudaTile(FragmentTileBase):
    def __init__(self, xvar=None, yvar=None, xsize=None, ysize=None):
        self.xvar=xvar
        self.yvar=yvar
        self.xsize = 0 if xsize is None else xsize
        self.ysize = 0 if ysize is None else ysize
        if self.xsize == 0 and self.ysize == 0:
            self.randomize_const()

    def new_vars(self):
        return [CUDA_CHUNK_VAR]

    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None and (self.xvar not in vars or self.yvar not in vars):
            if CHECK_VERBOSE:
                print ' * check fail cudaTile %s %s %r' % (self.xvar, self.yvar, vars)
            return False
        if not [isinstance(x, FragmentCudaTile) for x in L] == [0]*(len(L)-1)+[1]:
            return False
        # Only tile and unroll can be used on the variables within the CUDA special vars
        yindex = vars.index(self.yvar)
        remain_vars = set(vars[:yindex] + vars[yindex+2:])
        allowed_vars = set(vars[:yindex])
        #print 'allowed_vars', allowed_vars, 'remain_vars', remain_vars
        #print 'L', L
        for x in L:
            if isinstance(x, (FragmentVectorize, FragmentChunk)):
                #if x.var not in allowed_vars:
                if CHECK_VERBOSE:
                    print ' * check fail cudeTile FragmentVectorize %s %r' % (x.var, allowed_vars)
                return False
            chosen = []
            if isinstance(x, FragmentParallel):
                chosen.append(x.var)
#            if isinstance(x, FragmentTile):    # unroll(), tile() allowed inside cuda vars
#                chosen.extend([x.xvar, x.yvar])
            for v in chosen:
                if v == CUDA_CHUNK_VAR:
                    if CHECK_VERBOSE:
                        print ' * check fail cudaTile var fragment %s' % x.var
                    return False
                if v not in allowed_vars: #remain_vars:
                    if CHECK_VERBOSE:
                        print ' * check fail cudaTile var fragment %s not in remain vars' % x.var
                    return False
        return check_duplicates(self.__class__, L, func)

    @staticmethod
    def fromstring(xvar, yvar, xsize, ysize, func=None):
        return FragmentCudaTile(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize))

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        if len(vars)-1 <= 0:
            return None
        i = random.randrange(len(vars)-1)
        return cls(vars[i+1],vars[i])
    
    def __str__(self):
        return '.cudaTile(%s,%s,%d,%d)'%(self.xvar,self.yvar,self.xsize,self.ysize)

    def var_order(self, prev_order):      # if f(x y c) after tile(x,y,xi,yi) the loop ordering is for c: for y: for x: for yi: for xi   [c y x yi xi]
                                          # previously the loop ordering was  for c: for y: for x     [c y x]
        #print 'prev_order', prev_order
        #print self.xvar, self.yvar
        try:
            i = prev_order.index(self.yvar)
        except ValueError:
            raise BadScheduleError
        if i+1 >= len(prev_order):
            raise BadScheduleError((i, self.xvar, prev_order))
        if self.xvar != prev_order[i+1]:
            raise BadScheduleError()
        ans = prev_order[:i] + [CUDA_CHUNK_VAR] + prev_order[i+2:]      # We are the last fragment, and var_order() upstream is only used for chunking
        assert len(ans) == len(prev_order) - 1
        return ans

def reorder_and_cudatile(L):
    if len(L) < 2:
        return False
    return ([isinstance(x, FragmentReorder) for x in L] == [0]*(len(L)-2)+[1,0] and
            [isinstance(x, FragmentCudaTile) for x in L] == [0]*(len(L)-2)+[0,1])

class FragmentReorder(Fragment):
    # Actually calls Func.reorder()
    def __init__(self, vars=None, idx=None, perm=None):
        self.vars = vars
        self.idx = idx
        if perm is None:
            self.permutation = permutation.permutation(vars, idx)
        else:
            self.permutation = perm
        #self.pairwise_swaps = permutation.pairwise_swaps(vars, self.permutation)

    @staticmethod
    def fromstring(*L, **kw):
        return FragmentReorder(perm=L)
        #return FragmentReorder(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize), xnewvar=xnewvar, ynewvar=ynewvar)

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        #print 'fragments', root_func, func, cls, vars, extra_caller_vars
        n = permutation.factorial(len(vars))
        if n <= 1:
            return None
        i = random.randrange(1,n)
        return cls(vars=vars, idx=i)
        #return [cls(vars=vars, idx=i) for i in range(1,permutation.factorial(len(vars)))]     # TODO: Allow random generation so as to not loop over n!
    
    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None:
            for var in self.permutation:
                if var not in vars:
                    if CHECK_VERBOSE:
                        print ' * check fail reorder %r %s %r' % (self.permutation, var, vars)
                    return False
        if not default_check(self.__class__, L, func):
            if CHECK_VERBOSE:
                print ' * check default_check for reorder failed'
            return False
        if [isinstance(x, FragmentReorder) for x in L] == [0]*(len(L)-1)+[1]:
            return True
        if is_cuda():
            if reorder_and_cudatile(L):
                return True
        if CHECK_VERBOSE:
            print ' * check reorder failed by default'
        return False
    
    def __str__(self):
        #ans = ''
        #assert len(self.pairwise_swaps) >= 1
        #for (a, b) in self.pairwise_swaps:
        #    ans += '.transpose(%s,%s)'%(a,b)
        #return ans
        return '.reorder(' + ','.join(v for v in self.permutation) + ')'

    def var_order(self, prev_order):
        if VAR_ORDER_VERBOSE:
            print 'FragmentReorder.var_order prev_order=%r, permutation=%r' % (prev_order, self.permutation)
        permutation = list(reversed(self.permutation))
        orig_order = list(reversed(prev_order))      # Argument order (reversed)
        order = list(orig_order)
        try:
            indices = sorted([order.index(permutation[j]) for j in range(len(self.permutation))])
        except ValueError:
            raise BadScheduleError
        for (iperm, i) in enumerate(indices):
            order[i] = permutation[iperm]

        sub = [order[indices[j]] for j in range(len(indices))]
        if sub != list(permutation):
            print 'sub', sub
            print 'permutation', self.permutation
            print 'orig_order', orig_order
            print 'order', order
            raise ValueError

        if VAR_ORDER_VERBOSE:
            print 'FragmentReorder.var_order returning %r' % order

        return order


def get_fragment_classes():
    return ([FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentSplit, FragmentTile, FragmentReorder] +
            ([FragmentCudaTile] if is_cuda() else []))

fragment_map = {'root': FragmentRoot,
                'vectorize': FragmentVectorize,
                'parallel': FragmentParallel,
                'unroll': FragmentUnroll,
                'chunk': FragmentChunk,
                'split': FragmentSplit,
                'tile': FragmentTile,
                'reorder': FragmentReorder,
                'update': FragmentUpdate,
                'cudaTile': FragmentCudaTile,
                'cudaChunk': FragmentChunk,
                'bound': FragmentBound}

def fragment_fromstring(s, func):
    if '(' not in s:
        raise ValueError(s)
    paren1 = s.index('(')
    paren2 = s.index(')')
    name = s[:paren1]
    cls = fragment_map[name]
    rest = [x.strip() for x in s[paren1+1:paren2].split(',')]
    #print cls, rest
    #print 'fragment_fromstring |%s|' % s, cls, rest
    return cls.fromstring(*rest, func=func)
    
class MutateFailed(Exception):
    "Mutation can fail due to e.g. trying to add a fragment to f.chunk(c), which is a singleton schedule macro."
    
class FragmentList(list):
    "A list of schedule macros applied to a Func, e.g. f.root().vectorize(x).parallel(y)."
    def __init__(self, func, L):
        self.func = func
        list.__init__(self, L)
    
    def var_order(self):
        "Variable order assuming we are root."
        ans = list(reversed(halide.func_varlist(self.func)))
        #print 'FragmentList', self
        #print 'var_order(FragmentList)', ans
        for item in self[1:]:
            ans = item.var_order(ans)
            #print 'var_order(update)', ans
        return ans
        
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

    @staticmethod
    def fromstring(func, s):
        """
        Constructor from a string s such as 'f.root().parallel(y)' (same format as returned by str() method).
        """
        if len(s.strip()) == 0:
            return FragmentList(func, [])
        if not '.' in s:
            raise ValueError('FragmentList is missing .: %r'%s)
        dot = s.index('.')
        s = s[dot+1:]
        ans = []
        for part in s.split('.'):
            ans.append(fragment_fromstring(part, func))
        return FragmentList(func, ans)
        

    def new_vars(self):
        ans = []
        for x in self:
            ans.extend(x.new_vars())
        return list(sorted(set(ans)))
    
    def all_vars(self):
        return list(sorted(set(halide.func_varlist(self.func)) | set(self.new_vars())))
        
    def __repr__(self):
        return "'" + str(self) + "'" #return 'FragmentList(%s, %r)' % (self.func, repr([str(x) for x in list(self)]))
        
    def randomized_const(self):
        ans = FragmentList(self.func, [copy.copy(x) for x in self])
        for x in ans:
            x.randomize_const()
        #    print '->', x
        #print 'FragmentList, after randomization:'
        #print ans
        return ans
    
    def check(self, partial_schedule=None):
        vars = halide.func_varlist(self.func)
        #if len(self) == 0:
        #    if self.func.isReduction():
        #        return False
        var_order = list(reversed(vars))
        try:
            for (i, x) in enumerate(self):
                if not x.check(self, partial_schedule, self.func, var_order):
                    if CHECK_VERBOSE:
                        print ' * check failed on func %s, returning' % self.func.name()
                    return False
                if i > 0:
                    var_order = x.var_order(var_order)
                #vars = sorted(set(vars + x.new_vars()))
        except BadScheduleError:
            if CHECK_VERBOSE:
                print ' * check fragmentlist BadSchedule error failed, returning'
            return False
        return True

    def added_or_edited(self, root_func, extra_caller_vars, partial_schedule, vars=None, delta=0):
        assert isinstance(partial_schedule, Schedule)
        if vars is None:
            vars = halide.func_varlist(self.func)
        for j in range(MUTATE_TRIES):
            L = copy.copy(list(self))
            i = random.randrange(len(L)+1-delta)
            #all_vars = list(vars)
            #for fragment in L[:i]:
            #    all_vars.extend(fragment.new_vars())
            all_vars = FragmentList(self.func, L[:i]).var_order()
            L[i:i+delta] = [valid_random_fragment(root_func, self.func, all_vars, extra_caller_vars, partial_schedule)]
            ans = FragmentList(self.func, L)
#            print ans, ans.check()
            if ans.check():
#                print '-'*40
                return ans
        raise MutateFailed

    def added(self, root_func, extra_caller_vars, partial_schedule, vars=None):
        "Copy of current FragmentList that checks and has a single fragment added."
        ans = self.added_or_edited(root_func, extra_caller_vars, partial_schedule, vars, delta=0)
        assert len(ans) == len(self)+1
        return ans
        
    def removed(self):
        "Copy of current FragmentList that checks and has a single fragment removed."
        for j in range(MUTATE_TRIES):
            L = copy.copy(list(self))
            i = random.randrange(len(L))
            del L[i:i+1]
            ans = FragmentList(self.func, L)
            if ans.check():
                return ans
        raise MutateFailed

    def edited(self, root_func, extra_caller_vars, partial_schedule, vars=None):
        "Copy of current FragmentList that checks and has a single fragment edited."
        ans = self.added_or_edited(root_func, extra_caller_vars, partial_schedule, vars, delta=1)
        assert len(ans) == len(self)
        return ans

def valid_random_fragment(root_func, func, all_vars, extra_caller_vars, partial_schedule):
    while True:
        cls = random.choice(get_fragment_classes())
        fragment = cls.random_fragment(root_func, func, cls, all_vars, extra_caller_vars, partial_schedule)
        if fragment is not None: #len(fragments):
            return fragment
#    if len(fragments) == 0:    # empty fragments can happen legitimately for e.g. chunk of the root func
#        raise ValueError(('fragments is empty', cls, all_vars, func.name()))
#    fragment = random.choice(fragments)
#    return fragment

def schedules_depth(root_func, func, vars, depth=0, random=False, extra_caller_vars=[], partial_schedule=None):
    "Un-checked schedules (FragmentList instances) of exactly the specified depth for the given Func."
    assert partial_schedule is not None
#    print func
#    print vars
    if not random:
        raise NotImplementedError('schedules must be sampled randomly')
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
        for L in schedules_depth(root_func, func, vars, depth-1, random, partial_schedule=partial_schedule):
            #print 'schedules_depth recurses', L
            if not L.check():
                continue
            if func.name() == root_func.name():                         # TODO: Checking that the output is root should really be in check()
                if len(L) >= 1 and not isinstance(L[0], FragmentRoot):
                    continue
            #all_vars = list(vars)
            #for fragment in L:
            #    all_vars.extend(fragment.new_vars())
            all_vars = FragmentList(func, L).var_order()
            for cls in randomized(get_fragment_classes()):
                #print 'all_vars', all_vars
                fragment = cls.random_fragment(root_func, func, cls, all_vars, extra_caller_vars, partial_schedule)
                #for fragment in randomized(cls.fragments(root_func, func, cls, all_vars, extra_caller_vars)):
                    #print 'fragment', fragment
                #print '=>', fragment
                    #print '*', len(L), L
                if fragment is not None:
                    yield FragmentList(func, list(L) + [fragment])

def schedules_func(root_func, func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, random=False, extra_caller_vars=[], vars=None, partial_schedule=None):
    """
    Generator of valid schedules for a Func, each of which is a FragmentList (e.g. f.root().vectorize(x).parallel(y)).
    
    If random is True then instead generate exactly one schedule randomly chosen.
    """
    assert partial_schedule is not None
    if vars is None:
        vars = halide.func_varlist(func)    
    #if func.name() == 'f':
    #    yield FragmentList(func, [random_module.choice(FragmentChunk.fragments(root_func, func, FragmentChunk, vars, extra_caller_vars))])
    #    return
    for depth in range(min_depth, max_depth+1):
        if random:
            depth = random_module.randrange(min_depth, max_depth+1)
        # TODO: This cannot be in check() because then we fail when trying to generate empty FragmentList [], but maybe should be pulled into a function
        if (func.name() == root_func.name() or func.isReduction()) and depth == 0:
            depth += 1
        for L in schedules_depth(root_func, func, vars, depth, random, extra_caller_vars, partial_schedule=partial_schedule):
            #print 'schedules_depth returns', L
            if L.check():
                #print '  check'
                yield L.randomized_const()
                if random:
                    return

class BadScheduleError(Exception):
    pass

def cuda_global_check(schedule):
    # All funcs chunk() or inline() (recursively), called by a cudaTile() func cannot be vectorize or parallel
    d_cuda = {}
    ok = [True]
    def callback(f, fparent):
        if fparent is None:
            return
        f_name = f.name()
        L = schedule.d.get(f_name, [])
        inline_chunk = (len(L) == 0 or isinstance(L[0], FragmentChunk))
        cuda = False
        if len(L) >= 1 and isinstance(L[-1], FragmentCudaTile):
            cuda = True
        if inline_chunk:
            if d_cuda.get(fparent.name(), False):
                cuda = True
                if sum([isinstance(x,(FragmentVectorize,FragmentParallel)) for x in L]) >= 1:
                    ok[0] = False
        d_cuda[f_name] = cuda
        #L = schedule.d[f_name]

    halide.visit_funcs(schedule.root_func, callback)
    if not ok[0]:
        return False
    
    # Also all funcs chunk() or inline() (recursively) and using cudaTile() cannot be parallel in a caller
    d_parallel = {}
    ok = [True]
    def callback(f, fparent):
        if fparent is None:
            return
        f_name = f.name()
        L = schedule.d.get(f_name, [])
        inline_chunk = (len(L) == 0 or isinstance(L[0], FragmentChunk))
        parallel = sum([isinstance(x, FragmentParallel) for x in L]) >= 1
        if inline_chunk:
            if d_parallel.get(fparent.name(), False):
                parallel = True
        if parallel:
            if sum([isinstance(x,(FragmentCudaTile)) for x in L]) >= 1:
                ok[0] = False
        d_parallel[f_name] = parallel
        #L = schedule.d[f_name]

    halide.visit_funcs(schedule.root_func, callback)
    if not ok[0]:
        return False
    
    return True

def schedule_str_from_cmdline(s):
    if not 'python autotune.py' in s:
        return s
    L = parseutil.split_doublequote(s)
    for x in L:
        if x.startswith('"') and x.endswith('"'):
            return x[1:-1]
    raise ValueError('bad .sh file')

class Schedule:
    """
    Schedule applying (recursively) to all Funcs called by root_func.
    
    For example:
        f.vectorize(x).parallel(y)
        g.parallel(y)
    """
    def __init__(self, root_func, d, genomelog='', generation=-1, index=-1, identity_str=None):
        self.root_func = root_func
        self.d = d
        self.genomelog = genomelog
        self.generation = generation
        assert isinstance(index, int)
        assert isinstance(generation, int)
        self.index = index
        self.identity_str = identity_str

    def oneline(self):
        "One line description for display."
        ans = str(self).replace('\n','\\n')
        maxlen = MAXLEN
        if len(ans) > maxlen:
            ans = ans[:maxlen-3] + '...'
        return "'" + ans + "'"

    def check(self, partial_schedule=None):
        all_funcs = halide.all_funcs(self.root_func)
        if set(self.d.keys()) != set(all_funcs): # and set(self.d.keys())|set(['input_clamped']) != set(all_funcs):
            print sorted(self.d.keys())
            print sorted(halide.all_funcs(self.root_func))
            raise ValueError(self, self.d.keys(), all_funcs.keys())
        for (fname, f) in all_funcs.items():
            if f.isReduction():
                L = self.d[fname]
                if len(L) == 0:
                    if CHECK_VERBOSE:
                        print ' * check failed, %s is reduction and inline' % fname
                    return False #raise ValueError(self)
        root_func_name = self.root_func.name()
        if root_func_name in self.d:
            L = self.d[root_func_name]
            if len(L) < 1 or not isinstance(L[0], FragmentRoot):
                if CHECK_VERBOSE:
                    print ' * check failed, root func %s is not root' % root_func_name
                return False
        else:
            return False
        if is_cuda() and not cuda_global_check(self):
            if CHECK_VERBOSE:
                print ' * cuda global check failed'
            return False
        #print 'check', self
        for (name, x) in self.d.items():
            assert x.func.name() == name
            if not x.check(partial_schedule):
                if CHECK_VERBOSE:
                    print ' * check on Schedule failed for %s' % x
                    print ' Full schedule:'
                    print self
                return False
        return True

    def title(self):
        return 'Schedule %s: %s' % (self.identity(), self.genomelog)
        
    def __copy__(self):
        d = {}
        for name in self.d:
            d[name] = copy.copy(self.d[name])
        return Schedule(self.root_func, d, self.genomelog, self.generation, self.index)
    
    def __str__(self):
        d = self.d
        ans = []
        for key in sorted(d.keys()):
            s = str(d[key])
            #if s != '':
            ans.append(s)
        return '\n'.join(ans) #join(['-'*40] + ans + ['-'*40])

    def __repr__(self):
        return 'Schedule(%s, %r)' % (self.root_func.name(), str(self))
        
    def identity(self):
        #print self.generation
        #print self.index
        return '%03d_%03d'%(self.generation,self.index) if self.identity_str is None else self.identity_str
    #def hash(self):
    #    return md5.md5(str(self)).hexdigest()[:10]
        
    def randomized_const(self):
        dnew = {}
        for name in self.d:
            dnew[name] = self.d[name].randomized_const()
        return Schedule(self.root_func, dnew)

    def new_vars(self):
        ans = []
        for x in self.d.values():
            ans.extend(x.new_vars())
        return list(sorted(set(ans)))

    def apply(self, constraints=None, verbose=False, check=False, scope={}):   # Apply schedule
        if check:
            if not self.check(self):
                raise BadScheduleError
        #return
        #verbose = True
        #print 'apply schedule:'
        #print str(self)
        #halide.inline_all(self.root_func)
        fscope = halide.all_vars(self.root_func)
        #print 'fscope', fscope.keys()
        new_vars = self.new_vars()
        if verbose:
            print 'apply, new_vars', new_vars
        for varname in new_vars:
            fscope[varname] = instantiate_var(varname)
        if verbose:
            print 'apply, fscope:', fscope
        def callback(f0, parent):
            name = f0.name()
            if verbose:
                print 'apply, name', name, constraints
            if constraints is not None and name in constraints.exclude_names:
                if verbose:
                    print '  constrained, skipping'
                return
            if name in self.d:
                s = str(self.d[name])
                update_parent = halide.update_func_parent(f0)
                f = update_parent.update() if update_parent is not None else f0
                f.reset()
                s = s.replace(name + '.', '__func.')
                fscope['__func'] = f
                fscope[name] = f
                if CHUNK_ROOT:
                    fscope['root'] = halide.root
                #print 'apply', s
                #print scope, s
                if verbose:
                    print '  exec', s
                try:
                    exec s in fscope
                except NameError:
                    raise BadScheduleError
            else:
                if verbose:
                    print '  not in d, reset'
                f.reset()
        halide.visit_funcs(self.root_func, callback)
        if 'tune_constraints' in scope:
            exec scope['tune_constraints'] in fscope
            
    def test(self, shape, input, constraints, eval_func=None):
        """
        Test on zero array of the given shape. Return evaluate() function which when called returns the output Image.
        """
        print 'apply'
        self.apply(constraints)
        print 'in_image'
        in_image = numpy.zeros(shape, input.type().to_numpy())
        print 'filter'
        return halide.filter_image(input, self.root_func, in_image, eval_func=eval_func)
    
    @staticmethod
    def fromstring(root_func, s, genomelog='', generation=-1, index=-1, fix=True):
        """
        Constructor from a string s such as 'f.root().parallel(y)\ng.chunk(y)' (same format as returned by str() method).
        
        If fix is True then tries to auto-fix human schedules into the strict internal schedule format, which requires:
         - Every func begins with its caller schedule e.g. f.root().parallel(y) is valid but f.parallel(y) is invalid
         - Reduction functions rf to be scheduled explicitly as either rf.root()|rf.chunk().
         - Output func g to be scheduled for its call schedule explicitly as g.root()...
        """
        s = schedule_str_from_cmdline(s)
        if '\\n' in s:
            raise ValueError('Bad newline character in %r'%s)
        #print 'Schedule.fromstring', root_func, s
        all_funcs = halide.all_funcs(root_func)
        root_func = root_func
        #print 'Schedule.fromstring', repr(s)
        #print s.strip().split('\n')
        #sys.exit(1)
        d = {}
        for line in s.strip().split('\n'):
            line = line.strip()
            if len(line) == 0:
                continue
            if not '.' in line:
                raise ValueError(s)
            dot = line.index('.')
            name = line[:dot]
            if name in d:
                raise KeyError('duplicate func %r in schedule' % name)
            d[name] = FragmentList.fromstring(all_funcs[name], line)
        
        ans = {}
        def callback(f, parent):
            name = f.name()
            if name not in d:
                ans[name] = FragmentList.fromstring(f, '')
            else:
                ans[name] = d[name]
        halide.visit_funcs(root_func, callback)
        
        if fix:
            # Make output be root()
            root_func_name = root_func.name()
            if not root_func_name in ans or len(ans[root_func_name]) == 0:
                ans[root_func_name] = FragmentList(root_func, [FragmentRoot()])
                
            # Inject root() as caller schedule if missing
            for (fname, f) in halide.all_funcs(root_func).items():
                if fname in ans:
                    L = ans[fname]
                    if len(L) > 0 and not (isinstance(L[0], FragmentChunk) or isinstance(L[0], FragmentRoot)):
                        ans[fname] = FragmentList(L.func, [FragmentRoot()] + list(L))
            
            # Inject root() for reductions (the default for reductions is root()).
            for (fname, f) in halide.all_funcs(root_func).items():
                if f.isReduction():
                    if not fname in ans or len(ans[fname]) == 0:
                        ans[fname] = FragmentList(f, [FragmentRoot()])
            
        return Schedule(root_func, ans, genomelog, generation, index)
        

def trivial_func_schedule(f):
    return FragmentList(f, [FragmentRoot()])
    
def random_schedule(root_func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, vars=None, constraints={}, max_nontrivial=None, grouping=None):
    """
    Generate Schedule for all functions called by root_func (recursively). Same arguments as schedules_func().
    """
    if vars is None:
        vars = halide.func_varlist(root_func)
    
    funcs = halide.all_funcs(root_func)
    
    while 1:
        chosen = set(funcs)
        
        d_new_vars = {}
        schedule = {}
        schedule_obj = Schedule(root_func, schedule, 'random', -2, -2, 'random')
        
        if max_nontrivial is not None and max_nontrivial < len(funcs):
            chosen = set(random.sample(list(funcs), max_nontrivial))
        if grouping is not None:
            chosen = [group[0] for group in grouping]
            
        def callback(f, parent):
            extra_caller_vars = d_new_vars.get(parent.name() if parent is not None else None,[])
    #        print 'schedule', f.name(), extra_caller_vars
    #        ans = schedules_func(root_func, f, min_depth, max_depth, random=True, extra_caller_vars=extra_caller_vars, vars=vars).next()
            name = f.name()
            if name in constraints:
                schedule[name] = constraints[name]
            else:
                if name in chosen:
                    max_depth_sel = max_depth # if f.name() != 'f' else 0
                    while 1:
                        try:
                            ans = schedules_func(root_func, f, min_depth, max_depth_sel, random=True, extra_caller_vars=extra_caller_vars, partial_schedule=schedule_obj).next()
                            break
                        except StopIteration:
                            continue
                    schedule[name] = ans
                else:
                    # Trivial schedule for func
                    schedule[name] = trivial_func_schedule(f)
            d_new_vars[name] = schedule[name].new_vars()
            
        halide.visit_funcs(root_func, callback)
        #print 'random_schedule', schedule
        try:
            schedule_obj.apply(check=True)
        except BadScheduleError:
            continue
        return schedule_obj

def func_lhs_var_names(f):
    ans = []
    for y in f.args():
        for x in y.vars():
            ans.append(x.name())
    return ans

def intersect_lists(L):
    "Take intersection while also preserving order (if possible)."
    if len(L) == 0:
        return []
    ans_set = reduce(set.intersection, [set(x) for x in L])
    added = set()
    ans = []
    for x in L[0]:
        if x not in added and x in ans_set:
            added.add(x)
            ans.append(x)
    return ans

def lower_bound_schedules(root_func):
    "Return a (far) lower bound estimate on number of schedules, by using chunk().tile()....tile() form schedules."
    max_tiles = DEFAULT_MAX_DEPTH-1
    d_callers = halide.callers(root_func)
    d_loop_vars = {}
    ans = [1]
    
    def callback(f, fparent):
        nchunk = 1
        if len(d_callers[f.name()]) == 1:
            mparent = len(halide.func_varlist(fparent))
            if mparent >= 2:
                nchunk = mparent + 2*max_tiles
        ans[0] *= nchunk*(nchunk+1)/2
        
        m = len(halide.func_varlist(f))
        if m < 2:
            return
        for i in range(max_tiles):
            ans[0] *= 36*(m-1)
            m += 2
        
    halide.visit_funcs(root_func, callback, toposort=True)
    return ans[0]

def test_intersect_lists():
    assert intersect_lists([['c', 'y', '_c1', '_c3', '_c2', '_c0', 'x'], ['c', 'z', 'y', 'x']]) == ['c', 'y', 'x']
    assert intersect_lists([[1,2,3,4,5],[1,3,5]]) == [1, 3, 5]
    assert intersect_lists([[1,3,5],[1,2,3,4,5]]) == [1, 3, 5]
    print 'valid_schedules.intersect_lists:     OK'

def chunk_vars(schedule, func, remove_inline=False, verbose=CHUNK_VARS_VERBOSE):
    d_stackL = {}      # Map f name to list of stacks (loop ordering) of variable names
    d_callers = halide.callers(schedule.root_func)
    d_func = halide.all_funcs(schedule.root_func)
    if verbose:
        print '======================================================='
        print 'CHUNK_VARS(%s)'%func.name()
        print '======================================================='
        print 'd_callers:', d_callers
    
    for fname in halide.toposort(d_callers):
        if fname == func.name():
            if verbose:
                print 'Found func %s, returning'%fname
            all_stacks = []
            for fparent in d_callers[fname]:
                all_stacks.extend(d_stackL[fparent])
            #ans = list(reversed(intersect_lists(d_stackL[fparent])))
            ans = list(reversed(intersect_lists(all_stacks)))
            if verbose:
                print '  Returning %r, all_stacks=%r' % (ans, all_stacks)
            return ans


        if len(d_callers[fname]) == 0:                      # Root function (no callers) is implicitly root()
            fragment = schedule.d[fname] if fname in schedule.d else FragmentList(d_func[fname], [])
            d_stackL[fname] = [fragment.var_order()]   # FIXMEFIXME: SHouldn't all var_order() be reversed here?
            if verbose:
                print 'No callers, %s, stack: %r' % (fname, d_stackL[fname])
        else:
            if fname in schedule.d:
                L = schedule.d[fname]
            else:
                L = FragmentList(d_func[fname], [FragmentRoot()] if d_func[fname].isReduction() else [])

            if len(L) >= 1 and isinstance(L[0], FragmentRoot):
                stackL = [L.var_order()]
                if verbose:
                    print '  root, stack order=', stackL
            else:
                # For each stack in each caller add to the new stack list
                stackL = []                         

                for fparent in d_callers[fname]:
                    if verbose:
                        print fname, fparent, '(', '_'.join(d_callers[fname]), ')'
                    if fparent not in d_stackL:
                        print 'fparent', fparent
                        print 'f', fname
                        print 'd_callers', d_callers
                        print 'd_stackL', d_stackL
                        print 'toposort', halide.toposort(d_callers)
                        raise ValueError
                    for stack in d_stackL[fparent]:
                        if verbose:
                            print '  parent stack:', stack
                        #L = schedule.d[fparent] if fparent in schedule.d else FragmentList(d_func[fparent], [])
                            
                        #L = schedule.d[fname] if fname in schedule.d else FragmentList(d_func[fname], [])
                        if len(L) == 0:
                            if verbose:
                                print '  inline, no stack changes'
                            pass        # No stack changes
                        elif isinstance(L[0], FragmentChunk):
                            if verbose:
                                print '  chunk'
                                print '  schedule', repr(schedule)
                                print '  L', L
                            order = list(reversed(halide.func_varlist(L.func)))         # FIXME: can reorder() reorder the caller variables?
                            if verbose:
                                print '  initial order', order
                            for fragment in L[1:]:
                                order = fragment.var_order(order)
                                if verbose:
                                    print '  update order', order
                            try:
                                i = len(stack)-1 - stack[::-1].index(L[0].var)
                            except ValueError:
                                raise BadScheduleError
                            stack = stack[:i+1]
                            stack.extend(order)
                            if verbose:
                                print '  result stack', stack
                        else:
                            raise ValueError((fname, fparent, L[0]))
                        stackL.append(stack)
                        
            d_stackL[fname] = stackL #list(stackL[0]) #sorted(reduce(set.intersection, stackL))        # FIXME: Not clear how to intersect two stacks...
            if verbose:
                print 'stackL', stackL
                print 'final stack:', d_stackL[fname]
                print '-'*20
                print
        #if fname == func.name():
        #    raise ValueError
        #    #stackL = [set(x) for x in d_stackL[fname]]
        #    #ans = sorted(reduce(set.intersection, stackL))
        #    ans = list(reversed(intersect_lists(d_stackL[fname])))
        #    if verbose:
        #        print 'return d_stackL', d_stackL[fname]
        #        print 'return stackL', stackL
        #        print 'return ans', ans
        #    return ans
    raise ValueError
        #stack[fname] = 1
        #print 'callers', fname, d_callers[fname]
        # Need to implement: reorder(), split(), tile(), stack popping
        
            
def test_chunk_vars_subproc(test_index=0):
    f = halide.Func('valid_f')
    g = halide.Func('valid_g')
    h = halide.Func('valid_h')
    x, y = halide.Var('valid_x'), halide.Var('valid_y')
    
    f[x,y]=x+y
    g[x,y]=f[x,y]
    h[x,y]=g[x,y]
    for remove_inline in [False, True]:
        #print chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root().tile(valid_x,valid_y,_c2,_c3,8,8)'), f, remove_inline)
        #sys.exit(1)
        
        assert Schedule.fromstring(h, '').check()
        assert Schedule.fromstring(h, 'valid_g.root()').check()
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root()'), f, remove_inline) == ['valid_x', 'valid_y']
        #cv = chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)'), f, remove_inline)
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)'), f, remove_inline) == ['_c0', '_c1', 'valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().split(valid_x,valid_x,_c0,8)'), f, remove_inline) == ['_c0', 'valid_x', 'valid_y']
#        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().split(valid_x,valid_x,_c0,8)\nvalid_g.chunk(valid_y)'), f, remove_inline) == ['_c0', 'valid_x', 'valid_y']
        #print chunk_vars(Schedule.fromstring(h, 'valid_h.root().split(valid_x,valid_x,_c0,8)\nvalid_g.chunk(valid_y)'), f, remove_inline)
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().split(valid_x,valid_x,_c0,8)\nvalid_g.chunk(valid_y)'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root()'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root().tile(valid_x,valid_y,_c2,_c3,8,8)'), f, remove_inline) == ['_c2', '_c3', 'valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root().parallel(valid_y)'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, ''), f, remove_inline) == ['valid_x', 'valid_y']

    # None of these schedules should pass. TODO: Also add some positive examples
    if test_index == 0:     # Boxblur (cumsum)
        from examples.boxblur_cumsum import filter_func
    
        L = ['output.root()\n\nsum_clamped.root().unroll(x,4).tile(x,y,_c0,_c1,64,16).split(_c1,_c1,_c2,4)\nsum.root()\nsumx.chunk(_c1).reorder(c,x,y)\nweight.root()',
             'output.root().parallel(x)\n\nsum_clamped.chunk(c).split(y,y,_c0,16).parallel(c)\nsum.root()\nsumx.chunk(_c0).tile(y,c,_c0,_c1,8,8).split(x,x,_c2,32)\nweight.root()',
             'output.root()\n\nsum_clamped.root().vectorize(y,8).tile(x,y,_c0,_c1,4,4).tile(_c0,_c1,_c2,_c3,32,32)\nsum.root()\nsumx.chunk(_c2).vectorize(x,4).tile(x,y,_c0,_c1,16,16).unroll(y,4)\nweight.chunk(x).tile(x,y,_c0,_c1,64,64).tile(x,y,_c2,_c3,4,4)',
             'output.root().tile(x,y,_c0,_c1,16,2).split(_c1,_c1,_c2,4).parallel(c)\nsum.chunk(_c2).unroll(x,8).parallel(x)\nsum_clamped.chunk(y)\nsumx.chunk(y)\nweight.root().vectorize(y,2)',
             'output.root().tile(x,y,_c0,_c1,64,64).tile(x,y,_c2,_c3,2,2).unroll(y,64)\nsum.root()\nsum_clamped.chunk(_c2).split(c,c,_c0,16).vectorize(_c0,4).reorder(_c0,c,y,x)\nsumx.chunk(_c0)\n',
             'output.root().parallel(y).vectorize(x,16)\nsum.chunk(x).vectorize(c,16)\nsum_clamped.chunk(x).tile(x,y,_c0,_c1,8,2).parallel(x)\nsumx.chunk(_c0).vectorize(c,8).unroll(y,2).unroll(x,32)\nweight.root().parallel(y).vectorize(y,2).parallel(x)',
             'output.root()\nsum.root()\nsum_clamped.root().split(x,x,_c0,32)\nsumx.chunk(_c0).tile(x,y,_c0,_c1,64,64)\nweight.root().tile(x,y,_c0,_c1,8,8).parallel(x)',
             'output.root()\n\nsum_clamped.root().split(x,x,_c0,4).tile(_c0,y,_c1,_c2,64,32)\nsum.root()\nsumx.chunk(_c2).tile(x,y,_c0,_c1,4,4)\nweight.chunk(x).unroll(x,2)',
             'output.root().parallel(y).split(x,x,_c0,8).tile(y,c,_c1,_c2,2,2)\nsum.root()\nsum_clamped.chunk(_c2).unroll(c,8).vectorize(c,4).unroll(y,2)\nsumx.chunk(_c2)\nweight.chunk(_c2).tile(x,y,_c0,_c1,64,64).split(x,x,_c2,16).reorder(_c1,x,y,_c0)',
             'output.root().tile(x,y,_c0,_c1,4,4).split(_c0,_c0,_c2,8).split(x,x,_c3,8)\nsum.root()\nsum_clamped.chunk(_c0)\nsumx.chunk(_c3).unroll(x,64).split(x,x,_c0,8).tile(y,c,_c1,_c2,16,2)\nweight.chunk(x).vectorize(x,2)',
             'output.root().split(y,y,_c0,2)\nsum.root()\nsum_clamped.chunk(y).split(c,c,_c0,2).vectorize(_c0,16).parallel(y)\nsumx.chunk(_c0).split(c,c,_c0,16).unroll(x,64)\nweight.chunk(x).vectorize(x,8)',
             'output.root().tile(y,c,_c0,_c1,4,4).unroll(c,4)\nsum.chunk(c)\n\nsumx.chunk(_c1)\nweight.chunk(c).parallel(y).tile(x,y,_c0,_c1,32,4).unroll(_c0,16)',
             'output.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y)\nsum.root()\nsumx.chunk(_c1)']
        (input, out_func, evaluate, scope) = filter_func()
    elif test_index == 1:      # Bilateral grid
        from examples.bilateral_grid import filter_func
        # Compiler error of chunk(iv0) schedule: Could not schedule clamped as chunked over _c1, _c1
        L = ['blurx.chunk(z)\nblury.chunk(z).vectorize(x,4).unroll(iv0,16).reorder(iv0,z,x,y)\nblurz.root().tile(z,iv0,_c0,_c1,4,4)\nclamped.chunk(y).vectorize(y,16)\ngrid.chunk(_c0)\ninterpolated.root().vectorize(x,2).reorder(y,x,iv0)\nsmoothed.root()',
            'blurx.chunk(iv0)\n\nblurz.chunk(y).unroll(y,8).parallel(x).unroll(iv0,8)\nclamped.chunk(_c1)\ngrid.root().vectorize(x,8).unroll(c,16)\n\nsmoothed.root().tile(y,c,_c0,_c1,64,32).tile(_c1,y,_c2,_c3,64,64)']
        (input, out_func, evaluate, scope) = filter_func()
        assert sorted(halide.callers(scope['smoothed'])['clamped']) == ['grid', 'interpolated']
    else:                      # Blur
        from examples.blur_clamped import filter_func
        L = ['blur_y.root().tile(x,y,_c0,_c1,4,4).reorder(y,x,c,_c1,_c0)\ninput_clamped.chunk(x,c).split(c,c,_c0,4).parallel(c)']
        (input, out_func, evaluate, scope) = filter_func()
        
    L = [Schedule.fromstring(out_func, x, fix=False) for x in L]
#    n = sum([x.check(x) for x in L])
#    assert n == 0, n
    errL = []
    for (i, x) in enumerate(L):
        if x.check(x):
            errL.append(repr((i, x)))

    trace_schedule = False #True if test_bilateral else False
    if trace_schedule:
        schedule = L[0]
        print '='*80
        print 'clamped chunk vars:'
        print chunk_vars(schedule, halide.all_funcs(schedule.root_func)['clamped'], verbose=True)
        print
        print '='*80
    
#    print
#    cv = chunk_vars(schedule, halide.all_funcs(schedule.root_func)['sum'])
#    print 'sum chunk vars:', cv
#    print
#    print '='*80

    if len(errL):
        print len(errL), 'errors out of', len(L)
        print '\n\n'.join(errL)
        raise ValueError
#    schedule = Schedule.fromstring(out_func, )
#    schedule = Schedule.fromstring(out_func, 'output.root().parallel(x)\n\nsum_clamped.chunk(c).split(y,y,_c0,16).parallel(c)\nsumx.chunk(_c0).tile(y,c,_c0,_c1,8,8).split(x,x,_c2,32)\nweight.root()')
    #schedule = Schedule.fromstring(out_func, 'output.root()\n\nsum_clamped.root().vectorize(y,8).tile(x,y,_c0,_c1,4,4).tile(_c0,_c1,_c2,_c3,32,32)\nsumx.chunk(_c2).vectorize(x,4).tile(x,y,_c0,_c1,16,16).unroll(y,4)\nweight.chunk(x).tile(x,y,_c0,_c1,64,64).tile(x,y,_c2,_c3,4,4)')
    #assert not schedule.check(schedule)

    print 'valid_schedules.chunk_vars.%d:        OK'%test_index

def test_valid_schedules():
    args = sys.argv[1:]
    if len(args) == 0:
        test_intersect_lists()
        for i in range(3):
            os.system('python ' + os.path.abspath(__file__) + ' test_chunk_vars %d'%i)
    elif args[0] == 'test_chunk_vars' and len(args) == 2:
        test_chunk_vars_subproc(int(args[1]))
    else:
        raise ValueError('bad command line parameters')
        
if __name__ == '__main__':
    test_valid_schedules()
