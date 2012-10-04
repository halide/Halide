
# - Plan on going to MIT all week next week
# - Take smaller steps usually in mutation (usually just 1?) -- so we can get to the optimal schedule with higher likelihood
# - Keep generating schedules until we get something valid
# - Keep more than just the top 5 when doing crossover?
# - For each generation, 128 individuals, their timing, where they come from
#    - Put complex heuristics as a mutate
#    - If making a choice that impacts fusion, skew choice as fanout to choose root. Or choose a very narrow path
#      if changed the tiling, say 10 new individuals that are the most likely, singular extensions in chess.
# - Equal probability for each type of mutation?

# - To disable crash dialog on Mac Google "disable crashreporter"

# - Add mutate swap() (swap order within FragmentList)
# - Make sure mutation is happening often enough (?), actually that each operation is happening in about fixed proportion.
# - tile() should be in the same order as the variables
# - Perhaps use maximum schedule fragment size of 5.

# - Do not nest ForkedWatchdog
# - Ideally do not use forking at all

# - Include history with each individual
# - Fix problem with extra_caller_vars not being set.
# - Seed according to percents rather than random.
# - Cross over between schedule fragments
# - Put in logic to tell if schedule is not valid in advance to gain some time
# - Can parallelize compile and also improve % valid schedules (for speed)

# - Run my recursive scheduler against PetaBricks
# - Dump top speed and schedule vs generation number out to some number of generations

# Determining schedules
# 1. Autotuning
# 2. Heuristics
#   - Parallelize outer dimension or two, vectorize inner. Instead of doing random splits focus on tiling splits
# 3. Machine learning

# Jonathan+Andrew discussion:
# - Tile size or unroll size needs to be divisible into input size
# - split(x, x, xi, n) is right syntax for now
# - Only vectorize at native vector widths, 2 4 8 16
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
from ForkedWatchdog import Watchdog
import tempfile
import subprocess
import examples
import md5
import signal
import multiprocessing
import threadmap
random_module = random

LOG_SCHEDULES = True      # Log all tried schedules (Fail or Success) to a text file
LOG_SCHEDULE_FILENAME = 'log_schedule.txt'
AUTOTUNE_VERBOSE = False #True #False #True
DEFAULT_MAX_DEPTH = 4
DEFAULT_IMAGE = 'apollo2.png'
DEFAULT_TESTER_KW = {'in_image': DEFAULT_IMAGE}
FORCE_TILE = False
MUTATE_TRIES = 10

# --------------------------------------------------------------------------------------------------------------
# Valid Schedule Enumeration
# --------------------------------------------------------------------------------------------------------------

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
        return True         # Inline
    else:
        # Handle singleton fragments
        if count(FragmentVectorize) > 1:        # Allow at most one vectorize per Func schedule (FragmentList)
            return False
        if FORCE_TILE and (len(L) >= 2 and count(FragmentTile) < 1):
            return False
        root_count = count(FragmentRoot)
        chunk_count = count(FragmentChunk)
        if isinstance(L[0], FragmentRoot) and root_count == 1 and chunk_count == 0:
            return True
        elif isinstance(L[0], FragmentChunk) and chunk_count == 1 and root_count == 0:
            return True
    return False
            
def make_fromstring(cls):
    @staticmethod
    def fromstring(var=None, value=None):
        return cls(var, int(value) if value is not None else value)
    return fromstring

class Fragment:
    "Base class for a single schedule macro applied to a Func, e.g. .vectorize(x), .parallel(y), .root(), etc."
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
        
    def randomize_const(self):
        "Randomize constants e.g. change vectorize(x, 8) => vectorize(x, (random value))."
    
    def check(self, L):
        "Given list of Schedule fragments (applied to a function) returns True if valid else False."
        return default_check(self.__class__, L)

class FragmentVarMixin:
    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
#        print 'fragments', cls
        return [cls(x) for x in vars]

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
            self.value = 0

    def randomize_const(self):
        self.value = blocksize_random()
        #print 'randomize_const, value=%d'% self.value

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
    def randomize_const(self):
        self.value = blocksize_random([2,4,8,16])

    def __str__(self):
        return '.vectorize(%s,%d)'%(self.var, self.value) #self.value) # FIXMEFIXME Generate random platform valid blocksize

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

for _cls in [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk]:
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
    def fromstring(var=None, var_repeat=None, newvar=None, value=None):
        return FragmentSplit(var, int(value) if value is not None else value, newvar)

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
    def __init__(self, xvar=None, yvar=None, newvar=None, vars=None, xnewvar=None, ynewvar=None, xsize=None, ysize=None):
        self.xvar=xvar
        self.yvar=yvar
        self.xsize = 0 if xsize is None else xsize
        self.ysize = 0 if ysize is None else ysize
        self.xnewvar = create_var(vars)                if xnewvar is None else xnewvar
        self.ynewvar = create_var(vars+[self.xnewvar]) if ynewvar is None else ynewvar

    def randomize_const(self):
        self.xsize = blocksize_random()
        self.ysize = blocksize_random()
        #print 'randomize_const, tile, size=%d,%d' % (self.xsize, self.ysize)

    def check(self, L):
        return check_duplicates(self.__class__, L)

    @staticmethod
    def fromstring(xvar, yvar, xnewvar, ynewvar, xsize, ysize):
        return FragmentTile(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize), xnewvar=xnewvar, ynewvar=ynewvar)

    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        ans = []
        for i in range(len(vars)-1):
            j = i+1
            #for j in range(i+1, len(vars)):
            ans.append(cls(vars[i],vars[j],vars=vars))
        return ans
#        return [cls(x,y,vars=vars) for x in vars for y in vars if x != y]

    def new_vars(self):
        return [self.xnewvar, self.ynewvar]
    
    def __str__(self):
        return '.tile(%s,%s,%s,%s,%d,%d)'%(self.xvar,self.yvar,self.xnewvar,self.ynewvar,self.xsize,self.ysize)

class FragmentTranspose(Fragment):
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
    def fromstring(*L):
        return FragmentTranspose(perm=L)
        #return FragmentTranspose(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize), xnewvar=xnewvar, ynewvar=ynewvar)

    @staticmethod
    def fragments(root_func, func, cls, vars, extra_caller_vars):
        return [cls(vars=vars, idx=i) for i in range(1,permutation.factorial(len(vars)))]     # TODO: Allow random generation so as to not loop over n!
    
    def check(self, L):
        if not default_check(self.__class__, L):
            return False
        return [isinstance(x, FragmentTranspose) for x in L] == [0]*(len(L)-1)+[1]
    
    def __str__(self):
        #ans = ''
        #assert len(self.pairwise_swaps) >= 1
        #for (a, b) in self.pairwise_swaps:
        #    ans += '.transpose(%s,%s)'%(a,b)
        #return ans
        return '.reorder(' + ','.join(v for v in self.permutation) + ')'

fragment_classes = [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentSplit, FragmentTile, FragmentTranspose]
fragment_map = {'root': FragmentRoot,
                'vectorize': FragmentVectorize,
                'parallel': FragmentParallel,
                'unroll': FragmentUnroll,
                'chunk': FragmentChunk,
                'split': FragmentSplit,
                'tile': FragmentTile,
                'reorder': FragmentTranspose}

def fragment_fromstring(s):
    if '(' not in s:
        raise ValueError(s)
    paren1 = s.index('(')
    paren2 = s.index(')')
    name = s[:paren1]
    cls = fragment_map[name]
    rest = [x.strip() for x in s[paren1+1:paren2].split(',')]
    #print cls, rest
    #print 'fragment_fromstring |%s|' % s, cls, rest
    return cls.fromstring(*rest)
    
class MutateFailed(Exception):
    "Mutation can fail due to e.g. trying to add a fragment to f.chunk(c), which is a singleton schedule macro."
    
class FragmentList(list):
    "A list of schedule macros applied to a Func, e.g. f.root().vectorize(x).parallel(y)."
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
            ans.append(fragment_fromstring(part))
        return FragmentList(func, ans)
        

    def new_vars(self):
        ans = []
        for x in self:
            ans.extend(x.new_vars())
        return list(sorted(set(ans)))
        
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
    
    def check(self):
        for x in self:
            if not x.check(self):
                return False
        return True

    def added_or_edited(self, root_func, extra_caller_vars, vars=None, delta=0):
        if vars is None:
            vars = halide.func_varlist(self.func)
        for j in range(MUTATE_TRIES):
            L = copy.copy(list(self))
            i = random.randrange(len(L)+1-delta)
            all_vars = list(vars)
            for fragment in L[:i]:
                all_vars.extend(fragment.new_vars())
            L[i:i+delta] = [random_fragment(root_func, self.func, all_vars, extra_caller_vars)]
            ans = FragmentList(self.func, L)
#            print ans, ans.check()
            if ans.check():
#                print '-'*40
                return ans
        raise MutateFailed

    def added(self, root_func, extra_caller_vars, vars=None):
        "Copy of current FragmentList that checks and has a single fragment added."
        ans = self.added_or_edited(root_func, extra_caller_vars, vars, delta=0)
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

    def edited(self, root_func, extra_caller_vars, vars=None):
        "Copy of current FragmentList that checks and has a single fragment edited."
        ans = self.added_or_edited(root_func, extra_caller_vars, vars, delta=1)
        assert len(ans) == len(self)
        return ans

def random_fragment(root_func, func, all_vars, extra_caller_vars):
    while True:
        cls = random.choice(fragment_classes)
        fragments = cls.fragments(root_func, func, cls, all_vars, extra_caller_vars)
        if len(fragments):
            break
#    if len(fragments) == 0:    # empty fragments can happen legitimately for e.g. chunk of the root func
#        raise ValueError(('fragments is empty', cls, all_vars, func.name()))
    fragment = random.choice(fragments)
    return fragment

def schedules_depth(root_func, func, vars, depth=0, random=False, extra_caller_vars=[]):
    "Un-checked schedules (FragmentList instances) of exactly the specified depth for the given Func."
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

def schedules_func(root_func, func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, random=False, extra_caller_vars=[], vars=None):
    """
    Generator of valid schedules for a Func, each of which is a FragmentList (e.g. f.root().vectorize(x).parallel(y)).
    
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
                yield L.randomized_const()
                if random:
                    return

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

    def check(self):
        for x in self.d.values():
            if not x.check():
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

    def identity(self):
        #print self.generation
        #print self.index
        return '%d_%d'%(self.generation,self.index) if self.identity_str is None else self.identity_str
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

    def apply(self, constraints, verbose=False):   # Apply schedule
        #verbose = True
        #print 'apply schedule:'
        #print str(self)
        #halide.inline_all(self.root_func)
        scope = halide.all_vars(self.root_func)
        #print 'scope', scope.keys()
        new_vars = self.new_vars()
        if verbose:
            print 'apply, new_vars', new_vars
        for varname in new_vars:
            scope[varname] = instantiate_var(varname)
        if verbose:
            print 'apply, scope:', scope
        def callback(f, parent):
            name = f.name()
            if verbose:
                print 'apply, name', name, constraints
            if name in constraints.exclude_names:
                if verbose:
                    print '  constrained, skipping'
                return
            if name in self.d:
                s = str(self.d[name])
                f.reset()
                s = s.replace(name + '.', '__func.')
                scope['__func'] = f
                #print 'apply', s
                #print scope, s
                if verbose:
                    print '  exec', s
                exec s in scope
            else:
                if verbose:
                    print '  not in d, reset'
                f.reset()
        halide.visit_funcs(self.root_func, callback)
        
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
    def fromstring(root_func, s, genomelog='', generation=-1, index=-1):
        """
        Constructor from a string s such as 'f.root().parallel(y)\ng.chunk(y)' (same format as returned by str() method).
        """
        #print 'Schedule.fromstring', root_func, s
        all_funcs = halide.all_funcs(root_func)
        root_func = root_func
        d = {}
        for line in s.strip().split('\n'):
            line = line.strip()
            if len(line) == 0:
                continue
            if not '.' in line:
                raise ValueError(s)
            dot = line.index('.')
            name = line[:dot]
            d[name] = FragmentList.fromstring(all_funcs[name], line)
        
        ans = {}
        def callback(f, parent):
            name = f.name()
            if name not in d:
                ans[name] = FragmentList.fromstring(f, '')
            else:
                ans[name] = d[name]
        halide.visit_funcs(root_func, callback)
        return Schedule(root_func, ans, genomelog, generation, index)

def random_schedule(root_func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, vars=None, constraints={}):
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
        name = f.name()
        if name in constraints:
            schedule[name] = constraints[name]
        else:
            max_depth_sel = max_depth # if f.name() != 'f' else 0
            ans = schedules_func(root_func, f, min_depth, max_depth_sel, random=True, extra_caller_vars=extra_caller_vars).next()
            schedule[name] = ans
        d_new_vars[name] = schedule[name].new_vars()
        
    halide.visit_funcs(root_func, callback)
    #print 'random_schedule', schedule
    return Schedule(root_func, schedule, 'random', -2, -2, 'random')

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

# --------------------------------------------------------------------------------------------------------------
# Autotuning via Genetic Algorithm (follows same ideas as PetaBricks)
# --------------------------------------------------------------------------------------------------------------

class AutotuneParams:
    #pop_elitism_pct = 0.2
    #pop_crossover_pct = 0.3
    #pop_mutated_pct = 0.3
    # Population sampling frequencies
    prob_pop = {'elitism': 0.2, 'crossover': 0.3, 'mutated': 0.3, 'random': 0.2}
    
    tournament_size = 5 #3
    mutation_rate = 0.15             # Deprecated -- now always mutates exactly once
    population_size = 128 #5 #32 #128 #64
    
    # Different mutation modes (mutation is applied to each Func independently)
    prob_mutate = {'replace': 0.2, 'consts': 0.2, 'add': 0.2, 'remove': 0.2, 'edit': 0.2}
    # 'replace' - Replace Func's schedule with a new random schedule
    # 'consts'  - Just modify constants when mutating
    # 'add'     - Add a single schedule macro to existing schedule
    # 'remove'  - Removing a single schedule macro from existing schedule
    # 'edit'    - Edit (replace) a single schedule macro within Func's schedule
    
    min_depth = 0
    max_depth = DEFAULT_MAX_DEPTH
    
    trials = 5                  # Timing runs per schedule
    generations = 500
    
    compile_timeout = 20.0 #15.0
    run_timeout_mul = 2.0 #3.0           # Fastest run time multiplied by this factor is cutoff
    run_timeout_default = 5.0       # Assumed 'fastest run time' before best run time is established
    
    crossover_mutate_prob = 0.15     # Probability that mutate() is called after crossover
    crossover_random_prob = 0.1      # Probability that crossover is run with a randomly generated parent
    
    num_print = 10

    parallel_compile_nproc = None   # Number of processes to use simultaneously for parallel compile (None defaults to number of virtual cores)

    tune_dir = None                 # Autotuning output directory or None to use a default directory
    
def sample_prob(d):
    "Randomly sample key from dictionary with probabilities given in values."
    items = d.items()
    Ptotal = sum(x[1] for x in items)
    cutoff = random.random()*Ptotal
    current = 0.0
    for (key, value) in items:
        current += value
        if current > cutoff:
            return key
    return key
    
def crossover(a, b, constraints):
    "Cross over two schedules, using 2 point crossover."
    a = constraints.constrain(a)
    b = constraints.constrain(b)
    funcL = halide.all_funcs(a.root_func, True)
    names = [x[0] for x in funcL]
    assert a.root_func is b.root_func
    aset = set(a.d.keys())
    bset = set(b.d.keys())
    assert aset == bset, (aset, bset) #== set(names)
    names = [x for x in names if x in aset]
    
    if random.randrange(2) == 0:
        (a, b) = (b, a)

    d = {}
    i1 = random.randrange(len(names))
    i2 = random.randrange(len(names))
    (i1, i2) = (min(i1, i2), max(i1, i2))
    if i1 == 0 and i2 == len(names)-1:
        i2 -= 1
    for i in range(len(names)):
        if i1 <= i <= i2:
            d[names[i]] = copy.copy(a.d[names[i]])
        else:
            d[names[i]] = copy.copy(b.d[names[i]])
    
    return Schedule(a.root_func, d)

def mutate(a, p, constraints):
    "Mutate existing schedule using AutotuneParams p."
    a0 = a
    a = copy.copy(a0)
    extra_caller_vars = []      # FIXME: Implement extra_caller_vars, important for chunk().
    
    while True:
#        for name in a.d.keys():
#            if random.random() < p.mutation_rate:
        name = random.choice(a.d.keys())
        dmutate = dict(p.prob_mutate)
        if len(a.d[name]) <= p.min_depth:
            del dmutate['remove']
        if len(a.d[name]) >= p.max_depth:
            del dmutate['add']
        if len(a.d[name]) == 0:
            del dmutate['edit']
#                if 'remove' in dmutate:
#                    del dmutate['remove']
#                if 'edit' in dmutate:
#                    del dmutate['edit']
        
        mode = sample_prob(dmutate)
        try:
            if mode == 'consts':
                a.d[name] = a.d[name].randomized_const()
                a.genomelog = 'mutate_consts(%s)'%a0.identity()
            elif mode == 'replace':
                constraints_d = a.d
                del constraints_d[name]
                all_d = random_schedule(a.root_func, p.min_depth, p.max_depth, None, constraints_d)
                a.d[name] = all_d.d[name]
                a.genomelog = 'mutate_replace(%s)'%a0.identity()
            elif mode == 'add':
                assert len(a.d[name]) < p.max_depth
                #raise NotImplementedError
                a.d[name] = a.d[name].added(a.root_func, extra_caller_vars)
                a.genomelog = 'mutate_add(%s)'%a0.identity()
            elif mode == 'remove':
                assert len(a.d[name]) > p.min_depth
                #raise NotImplementedError
                a.d[name] = a.d[name].removed()
                a.genomelog = 'mutate_remove(%s)'%a0.identity()
            elif mode == 'edit':
#                        raise NotImplementedError
                a.d[name] = a.d[name].edited(a.root_func, extra_caller_vars)
                a.genomelog = 'mutate_edit(%s)'%a0.identity()
            else:
                raise ValueError('Unknown mutation mode %s'%mode)
        except MutateFailed:
            continue
            
        try:
            #print 'Mutated schedule:' + '\n' + '-'*40 + '\n' + str(a) + '\n' + '-' * 40 + '\n'
            a.apply(constraints)       # Apply schedule to determine if random_schedule() invalidated new variables that were referenced
        except (NameError, halide.ScheduleError):
            continue
        return a

def select_and_crossover(prevL, p, root_func, constraints):
    a = tournament_select(prevL, p, root_func)
    b = tournament_select(prevL, p, root_func)
    if random.random() < p.crossover_random_prob:
        b = random_schedule(root_func, p.min_depth, p.max_depth)
    c = crossover(a, b, constraints)
    is_mutated = False
    if random.random() < p.crossover_mutate_prob:
        c = mutate(c, p, constraints)
        is_mutated = True
    c.genomelog = 'crossover(%s, %s)'%(a.identity(), b.identity()) + ('' if not is_mutated else '+'+c.genomelog.replace('(-1_-1)', '()'))
#    if is_mutated:
#        c.genomelog = 'XXXX'
#        c.identity_str = 'XXXXX'
    return c

def select_and_mutate(prevL, p, root_func, constraints):
    a = tournament_select(prevL, p, root_func)
    c = mutate(a, p, constraints)
    #c.genomelog = 'mutate(%s)'%a.identity()
    return c

def tournament_select(prevL, p, root_func):
    i = random.randrange(p.tournament_size)
    if i >= len(prevL):
        return random_schedule(root_func, p.min_depth, p.max_depth)
    else:
        return copy.copy(prevL[i])

class Constraints:
    "Constraints([f, g]) excludes f, g from autotuning."
    def __init__(self, exclude=[]):
        self.exclude = exclude
        self.exclude_names = set()
        for f in self.exclude:
            self.exclude_names.add(f.name())
        
    def __str__(self):
        return 'Constraints(%r)'%[x.name() for x in self.exclude]
        
    def constrain(self, schedule):
        "Return new Schedule instance with constraints applied."
#        if not 'tile' in str(schedule):
#            return random_schedule(schedule.root_func, 0, 0)
        d = {}
        for name in schedule.d:
            if name not in self.exclude_names:
                d[name] = schedule.d[name]
        return Schedule(schedule.root_func, d, schedule.genomelog, schedule.generation, schedule.index, schedule.identity_str)

class Duplicate(Exception):
    pass
    
def next_generation(prevL, p, root_func, constraints, generation_idx):
    """"
    Get next generation using elitism/mutate/crossover/random.
    
    Here prevL is list of Schedule instances sorted by decreasing fitness, and p is AutotuneParams.
    """
    ans = []
    schedule_strs = set()
    def append_unique(schedule):
        s = str(schedule).strip()
        if s not in schedule_strs:
            schedule_strs.add(s)
            schedule.generation = generation_idx
            schedule.index = len(ans)
            schedule.identity_str = None
            ans.append(schedule)
        else:
            raise Duplicate
            
    def do_crossover():
        append_unique(constraints.constrain(select_and_crossover(prevL, p, root_func, constraints)))
    def do_mutated():
        append_unique(constraints.constrain(select_and_mutate(prevL, p, root_func, constraints)))
    def do_random():
        append_unique(constraints.constrain(random_schedule(root_func, p.min_depth, p.max_depth)))
    def do_until_success(func):
        while True:
            try:
                func()
                return
            except Duplicate:
                continue

#    random_pct = 1-p.pop_mutated_pct-p.pop_crossover_pct-p.pop_elitism_pct

    for i in range(int(p.population_size*p.prob_pop['elitism'])):
        if i < len(prevL):
            current = copy.copy(prevL[i])
            if not '(elite copy of' in current.genomelog:
                current.genomelog += ' (elite copy of %s)' % current.identity() # FIXMEFIXME
            append_unique(current)
    
    # Normalize probabilities after removing elitism
    P_total = p.prob_pop['crossover'] + p.prob_pop['mutated'] + p.prob_pop['random']
    P_crossover = p.prob_pop['crossover']*1.0 / P_total
    P_mutated   = p.prob_pop['mutated']*1.0 / P_total
    P_random    = p.prob_pop['random']*1.0 / P_total
    
    nrest = p.population_size - len(ans)
    ncrossover = int(P_crossover*nrest)
    nmutated = int(P_mutated*nrest)
    nrandom = nrest-ncrossover-nmutated
    for i in range(ncrossover):
#        print 'crossover %d/%d'%(i, ncrossover)
        do_until_success(do_crossover)
    for i in range(nmutated):
#        print 'mutated %d/%d'%(i,nmutated)
        do_until_success(do_mutated)
    for i in range(nrandom):
#        print 'random %d/%d'%(i,nrandom)
        do_until_success(do_random)
       
    assert len(ans) == p.population_size, (len(ans), p.population_size)
    """
    Pd = {'crossover': p.pop_crossover_pct, 'mutated': p.pop_mutated_pct, 'random': random_pct}

    while len(ans) < p.population_size:
        mode = sample_prob(Pd)
        if mode == 'crossover':
            do_crossover()
        elif mode == 'mutated':
            do_mutated()
        elif mode == 'random':
            do_random()
        else:
            raise ValueError('Unknown mode %s'%mode)
    """
    
    return ans

class AutotuneTimer:
    compile_time = 0.0
    run_time = 0.0
    total_time = 0.0
    def __init__(self):
        self.start_time = time.time()
    
def time_generation(L, p, test_gen_func, timer, constraints, display_text=''):
    #T0 = time.time()
    #Tcompile = [0.0]
    #Trun = [0.0]
    test_gen_iter = iter(test_gen_func(L, constraints))
    def time_schedule():
        info = test_gen_iter.next() #test_func(current, constraints)()
        #print 'time_schedule', current, info
        timer.compile_time += info['compile_avg']
        timer.run_time += info['run']
        return info['time']
        #out = test_func(current)
        #current.apply()
        #T = []
        #for i in range(p.trials):
        #    dout = out()
        #    T.append(dout['time'])
        #return min(T) #sum(T)/len(T)
    
    ans = []
    success = 0
    for i in range(len(L)):
        Tstart = time.time()
        if AUTOTUNE_VERBOSE:
            print 'Timing %d/%d'%(i, len(L)),
        ans.append(time_schedule())
        #ans.append(time_schedule(L[i]))
        success += get_error_str(ans[-1]) is None #< COMPILE_TIMEOUT
        timer.total_time = time.time()-timer.start_time
        stats_str = 'pid=%d, %.0f%% succeed, compile time=%d secs, run time=%d secs, total=%d secs' % (os.getpid(), success*100.0/(i+1),timer.compile_time, timer.run_time, timer.total_time)
        if AUTOTUNE_VERBOSE:
            print '%.5f secs'%ans[-1]
        else:
            sys.stderr.write('\n'*100 + 'Testing %d/%d (%s)\n%s\n'%(i+1,len(L),stats_str,display_text))
            sys.stderr.flush()
            pass
    print 'Statistics: %s'%stats_str
    return ans

def log_sched(schedule, s, no_output=False, f=[]):
    if LOG_SCHEDULES:
        if len(f) == 0:
            f.append(open(LOG_SCHEDULE_FILENAME,'wt'))
        if no_output:
            return
        f[0].write('-'*40 + '\n' + schedule.title() + '\n' + str(schedule) + '\n' + s + '\n')
        f[0].flush()

COMPILE_TIMEOUT = 10001.0
COMPILE_FAIL    = 10002.0
RUN_TIMEOUT     = 10003.0
RUN_FAIL        = 10004.0

def get_error_str(timeval):
    "Get error string from special (high-valued) timing value (one of the above constants)."
    d = {COMPILE_TIMEOUT: 'COMPILE_TIMEOUT',
         COMPILE_FAIL:    'COMPILE_FAIL',
         RUN_TIMEOUT:     'RUN_TIMEOUT',
         RUN_FAIL:        'RUN_FAIL'}
    if timeval in d:
        return d[timeval]
    return None

SLEEP_TIME = 0.01

def wait_timeout(proc, timeout, T0=None):
    "Wait for subprocess to end (returns return code) or timeout (returns None)."
    if T0 is None:
        T0 = time.time()
    while True:
        p = proc.poll()
        if p is not None:
            return p
        if time.time() > T0+timeout:
            return None
        time.sleep(SLEEP_TIME)

def run_timeout(L, timeout, last_line=False, time_from_subproc=False):
    """
    Run shell command in list form, e.g. L=['python', 'autotune.py'], using subprocess.
    
    Returns None on timeout otherwise str output of subprocess (if last_line just return the last line).
    
    If time_from_subproc is True then the starting time will be read from the first stdout line of the subprocess (seems to have a bug).
    """
    fout = tempfile.TemporaryFile()
    proc = subprocess.Popen(L, stdout=fout, stderr=fout)
    T0 = None
    if time_from_subproc:
        while True:
            fout.seek(0)
            fout_s = fout.readline()
            try:
                T0 = float(fout_s)
                break
            except ValueError:
                pass
            time.sleep(SLEEP_TIME)
        print 'Read T0: %f'% T0
        #sys.exit(1)
    status = wait_timeout(proc, timeout, T0)
    if status is None:
        try:
            proc.kill()
        except OSError:
            pass
        return None

    fout.seek(0)
    ans = fout.read()
    if last_line:
        ans = ans.strip().split('\n')[-1].strip()
    return ans

def default_tester(input, out_func, p, filter_func_name, in_image, allow_cache=True):
    cache = {}
    best_run_time = [p.run_timeout_default]

    nproc = p.parallel_compile_nproc
    if nproc is None:
        nproc = multiprocessing.cpu_count()
    
    #def signal_handler(signum, stack_frame):            # SIGCHLD is sent by default when child process terminates
    #    f = open('parent_%d.txt'%random.randrange(1000**2),'wt')
    #    f.write('')
    #    f.close()
    #signal.signal(signal.SIGCONT, signal_handler)
    
    def test_func(scheduleL, constraints):       # FIXME: Handle constraints
        def subprocess_args(schedule, schedule_str, compile=True):
            binary_file = os.path.join(p.tune_dir, schedule.identity())
            mode_str = 'compile' if compile else 'run'
            return ['python', 'autotune.py', 'autotune_%s_child'%mode_str, filter_func_name, schedule_str, in_image, '%d'%p.trials, str(os.getpid()), binary_file]
        # Compile all schedules in parallel
        def compile_schedule(i):
            schedule = scheduleL[i]
            schedule_str = str(schedule)
            if schedule_str in cache:
                return cache[schedule_str]
            
            T0 = time.time()
            out = run_timeout(subprocess_args(schedule, schedule_str, True), p.compile_timeout, last_line=True)
            Tcompile = time.time()-T0
            
            if out is None:
                return {'time': COMPILE_TIMEOUT, 'compile': Tcompile, 'run': 0.0}
            if not out.startswith('Success'):
                return {'time': COMPILE_FAIL, 'compile': Tcompile, 'run': 0.0}
            return {'time': 0.0, 'compile': Tcompile, 'run': 0.0}
        
        Tbegin_compile = time.time()
        shuffled_idx = range(len(scheduleL))
        random.shuffle(shuffled_idx)
        compiledL = threadmap.map(compile_schedule, shuffled_idx, n=nproc)
        Ttotal_compile = time.time()-Tbegin_compile
        
        assert len(compiledL) == len(scheduleL)
        
        # Run schedules in serial
        def run_schedule(i):
            schedule = scheduleL[i]
            compiled_ans = compiledL[i]
            if get_error_str(compiled_ans['time']) is not None:
                return compiled_ans
                
            schedule_str = str(schedule)
            if schedule_str in cache:
                return cache[schedule_str]

            T0 = time.time()
            out = run_timeout(subprocess_args(schedule, schedule_str, False), best_run_time[0]*p.run_timeout_mul, last_line=True)
            Trun = time.time()-T0
            
            if out is None:
                return {'time': RUN_TIMEOUT, 'compile': compiled_ans['compile'], 'run': Trun}
            if not out.startswith('Success') or len(out.split()) != 2:
                return {'time': RUN_FAIL, 'compile': compiled_ans['compile'], 'run': Trun}
            T = float(out.split()[1])
            best_run_time[0] = min(best_run_time[0], T)
            
            return {'time': T, 'compile': compiled_ans['compile'], 'run': Trun}
            
        runL = map(run_schedule, range(len(scheduleL)))
        
        for i in range(len(scheduleL)):
            schedule = scheduleL[i]
            runL[i]['compile_avg'] = Ttotal_compile/len(scheduleL)
            current = cache[str(scheduleL[i])] = runL[i]
            
            e = get_error_str(current['time'])
            first_part = 'Error %s'%e if e is not None else 'Best time %.4f'%current['time']
            log_sched(schedule, '%s, compile=%.4f, run=%.4f'%(first_part, current['compile'], current['run']))
        return runL
        
    return test_func
    
    """
    def test_func(schedule, constraints):
        schedule_str = str(schedule).strip()
        if schedule_str in cache and allow_cache:
            return lambda: cache[schedule_str]
        try:
            T0 = time.time()
            with Watchdog(p.compile_timeout):
                schedule.apply(constraints)
                evaluate = halide.filter_image(input, out_func, in_image)
                Tend_compile = time.time()
                Tcompile = Tend_compile-T0
                if AUTOTUNE_VERBOSE:
                    print 'compiled in', Tcompile, repr(str(schedule))
        except Watchdog:
            if AUTOTUNE_VERBOSE:
                print 'compile failed', repr(str(schedule))
            ans = {'time': COMPILE_TIMEOUT, 'compile': time.time()-T0, 'run': 0.0}
            cache[schedule_str] = ans
            log_sched(schedule_str, 'Fail compile, compile=%.4f, run=%.4f'%(ans['compile'], ans['run']))
            return lambda: ans
        
        def out():
            T = []
            for i in range(p.trials):
                T0 = time.time()

                try:
                    with Watchdog(best_run_time[0]*p.run_timeout_mul):
                        Iout = evaluate()
                except Watchdog:
                    if AUTOTUNE_VERBOSE:
                        print 'run failed', repr(str(schedule))
                    ans = {'time': RUN_TIMEOUT, 'compile': Tcompile, 'run': time.time()-Tend_compile}
                    cache[schedule_str] = ans
                    log_sched(schedule_str, 'Fail run %d, compile=%.4f, run=%.4f'%(i, ans['compile'], ans['run']))
                    return ans

                T1 = time.time()
                T.append(T1-T0)
            T = min(T)
            if T < best_run_time[0]:
                best_run_time[0] = T
            ans = {'time': T, 'compile': Tcompile, 'run': time.time()-Tend_compile}
            cache[schedule_str] = ans
            log_sched(schedule_str, 'Success, compile=%.4f, run=%.4f, time_best=%.4f'%(ans['compile'], ans['run'], ans['time']))
            return ans
        return out

    return test_func
    """
"""

def default_tester(input, out_func, p, in_image, counter=[0]):
    evaluate = halide.filter_image(input, out_func, in_image)
    def test_func():
        out = evaluate()
        #out_np = numpy.asarray(out)
        #out.show()
        #out.save('out%d.png'%counter[0])
        counter[0] += 1
    return test_func
"""

def check_schedules(currentL):
    "Verify that all schedules are valid (according to .check() rules at least)."
    for schedule in currentL:
        if not schedule.check():
            raise ValueError('schedule fails check: %s'%str(schedule))

#def autotune(input, out_func, p, tester=default_tester, tester_kw={'in_image': 'lena_crop2.png'}):
def resolve_filter_func(filter_func_name):
    if '.' in filter_func_name:
        exec "import " + filter_func_name[:filter_func_name.rindex('.')]
    return eval(filter_func_name)

def autotune(filter_func_name, p, tester=default_tester, tester_kw=DEFAULT_TESTER_KW, constraints=Constraints(), seed_scheduleL=[]):
    timer = AutotuneTimer()

    p = copy.deepcopy(p)
    if p.tune_dir is None:
        p.tune_dir = os.path.join(os.path.abspath('.'), 'tune')
    if not os.path.exists(p.tune_dir):
        os.makedirs(p.tune_dir)
        
    log_sched(None, None, no_output=True)    # Clear log file

    random.seed(0)
    (input, out_func, evaluate_func, scope) = resolve_filter_func(filter_func_name)()
    test_func = tester(input, out_func, p, filter_func_name, **tester_kw)
    
    currentL = []
    for (iseed, seed) in enumerate(seed_scheduleL):
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, seed, 'seed(%d)'%iseed, 0, iseed)))
        #print 'seed_schedule new_vars', seed_schedule.new_vars()
#        currentL.append(seed_schedule)
#        currentL.append(Schedule.fromstring(out_func, ''))
#        currentL.
    display_text = ''
    check_schedules(currentL)
    
#    timeL = time_generation(currentL, p, test_func, timer, constraints, display_text)
#    print timeL
#    sys.exit(1)
    
    for gen in range(p.generations):
        currentL = next_generation(currentL, p, out_func, constraints, gen)
        check_schedules(currentL)
        
        timeL = time_generation(currentL, p, test_func, timer, constraints, display_text)
        
        bothL = sorted([(timeL[i], currentL[i]) for i in range(len(timeL))])
        display_text = '-'*40 + '\n'
        display_text += 'Generation %d'%(gen) + '\n'
        display_text += '-'*40 + '\n'
        for (j, (timev, current)) in list(enumerate(bothL))[:p.num_print]:
            current_s = '%15.4f'%timev
            e = get_error_str(timev)
            if e is not None:
                current_s = '%15s'%e
            display_text += '%s %-4s %s' % (current_s, current.identity(), repr(str(current))) + '\n'
        display_text += '\n'
        print display_text
        sys.stdout.flush()
        
        currentL = [x[1] for x in bothL]

# --------------------------------------------------------------------------------------------------------------
# Unit Tests
# --------------------------------------------------------------------------------------------------------------

def test_crossover(verbose=False):
    (f, g) = test_funcs()
    constraints = Constraints()
    
    for j in range(8):
        random.seed(j)

#        for i in range(1000):
#            a = nontrivial_schedule(g)
#            a.apply()
#        print 'random_schedule:  OK'
        
        a = nontrivial_schedule(g)
        aL = str(a).split('\n')
        while True:
            b = nontrivial_schedule(g)
            bL = str(b).split('\n')
            all_different = True
            assert len(aL) == len(bL), (a, b, aL, bL)
            for i in range(len(aL)):
                if aL[i] == bL[i]:
                    all_different = False
                    break
            if all_different:
                break
        #print 'test_crossover'
        #print '----- Schedule A -----'
        #print a
        #print 
        #print '----- Schedule B -----'
        #print b
        #T0 = time.time()
        #print 'a'
        for i in range(80):
            c = crossover(a, b, constraints)
            c.apply(constraints)
            cL = str(c).split('\n')
            assert aL != bL and aL != cL and bL != cL
            if verbose:
                print '---- Crossover %d,%d ----'%(j,i)
                print c
        #T1 = time.time()
        
        p = AutotuneParams()
        #print 'b'
        for i in range(80):
            if verbose:
                print '---- Mutate after crossover %d,%d ----'%(j,i)
                print 'a', repr(str(a)), a.new_vars()
                print 'b', repr(str(b)), b.new_vars()
            c = crossover(a, b, constraints)
            if verbose:
                print 'c', repr(str(c)), c.new_vars()
            c.apply(constraints)
            c = mutate(c, p, constraints)
            if verbose:
                print 'cmutate', repr(str(c)), c.new_vars()
            c.apply(constraints)
    #        cL = str(c).split('\n')
    #        assert aL != bL and aL != cL and bL != cL

        #T2 = time.time()
        #print T1-T0, T2-T1
        
        def test_generation(L, prevL):
            assert len(L) == p.population_size
            for x in L:
                x.apply(constraints)
            current_set = set(str(x) for x in L)
            prev_set = set(str(x) for x in prevL)
            assert len(current_set) > 2
            if len(prev_set):
                assert len(current_set & prev_set) >= 1
        
        #random.seed(2)
        #print 'c'
        prev_gen = []
        for gen in range(2):
            L = next_generation(prev_gen, p, g, constraints)
            if j == 0 and verbose:
                for i in range(len(L)):
                    print 'gen=%d, i=%d'%(gen,i)
                    print L[i]
                    print '-'*20
            test_generation(L, prev_gen)
            prev_gen = L
    
    print 'crossover:         OK'
    print 'mutate:            OK'
    print 'next_generation:   OK'

def test_funcs(cache=[]):
    if len(cache):
        return cache[0]
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
    cache.append((f, g))
    return (f, g)

def nontrivial_schedule(g):
    while True:
        r = random_schedule(g, 0, DEFAULT_MAX_DEPTH)
        s = str(r)
        if len(s) > 30:
            contains_num = False
            for i in range(65):
                if str(i) in s:
                    contains_num = True
                    break
            if contains_num:
                return r

def test_sample_prob():
    d = {'a': 0.3, 'b': 0.5, 'c': 0.2, 'd': 0.0}
    count = 10000
    dc = {'a': 0, 'b': 0, 'c': 0, 'd': 0}
    for i in range(count):
        key = sample_prob(d)
        dc[key] += 1
    eps = 0.05
    for key in d.keys():
        assert abs(dc[key]*1.0/count-d[key])<eps, (key, d[key], dc[key]*1.0/count)
    assert dc['d'] == 0
    print 'sample_prob:       OK'

def test_schedules(verbose=False, test_random=False):
    #random_module.seed(int(sys.argv[1]) if len(sys.argv)>1 else 0)
    constraints = Constraints()
    halide.exit_on_signal()
    (f, g) = test_funcs()
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
    for i in range(1000):
        d = random_schedule(g, 1, DEFAULT_MAX_DEPTH)
        si = str(d)
        si2 = str(Schedule.fromstring(g, si))
        if si != si2:
            print '-'*40
            print 'd'
            print d
            print '-'*40
            print 'si'
            print si
            print '-'*40
            print 'si2'
            print si2
            raise ValueError
        
        s.append(si)
        if verbose:
            print 'Schedule:'
            print '-'*20
            print si
            print '-'*20
            print
        sys.stdout.flush()
        d.apply(constraints)
        if test_random:
            evaluate = d.test((36, 36, 3), input, Constraints())
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
    assert 'f.root().reorder' in s

    assert nvalid_random == 100
    if verbose:
        print 'generated in %.3f secs' % (T1-T0)

    print 'random_schedule:   OK'
    
    r = nontrivial_schedule(g)
    constantL = [str(r)]
#    print 'Randomizing the constants in a chosen schedule:'
    for i in range(100):
        #print r.randomized_const()
        #print
        constantL.append(str(r.randomized_const()))
    assert len(set(constantL)) > 1
    print 'randomized_const:  OK'
            
def test():
    random.seed(0)
    test_sample_prob()
#    test_schedules(True)
    test_schedules()
    test_crossover()
    
def main():
    args = sys.argv[1:]
    if len(args) == 0:
        print 'autotune test|print|autotune examplename|test_sched|test_fromstring|test_variations'
        sys.exit(0)
    if args[0] == 'test':
        test()
    elif args[0] == 'test_random':
        global use_random_blocksize
        use_random_blocksize = False
        test_schedules(True, test_random=True)
    elif args[0] == 'print':
        nprint = 100
        if len(args) > 1:
            nprint = int(args[1])
        cache = set()
        nsuccess = 0
        nfail = 0
        for i in range(nprint):
            if os.path.exists('_out.txt'):
                os.remove('_out.txt')
            os.system('python autotune.py test_random > _out.txt 2> /dev/null')
            s = open('_out.txt', 'rt').read()
            success = 'Success' in s
            search = 'Schedule:\n' + '-'*20
            try:
                j = s.index(search)
            except:
                raise ValueError('Did not find schedule in output')
            schedule = s[j:s.index('-'*20,j+len(search))]
            #print 'Found schedule', schedule
            nsuccess += success
            nfail += not success
            if schedule not in cache:
                print 'Success' if success else 'Failed ', schedule
                sys.stdout.flush()
                cache.add(schedule)
        print
        print 'Number successful schedules: %d/%d (%d failed)' % (nsuccess, nprint, nfail)
    elif args[0] == 'test_variations':
        filter_func_name = 'examples.blur.filter_func'
        (input, out_func, test_func, scope) = resolve_filter_func(filter_func_name)()

        seed_scheduleL = []
        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8)\n' +
                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        p = AutotuneParams()
        exclude = []
        for key in scope:
            if key.startswith('input_clamped'):
                exclude.append(scope[key])
        constraints = Constraints(exclude)
        p.population_size = 300 #1000
        p.tournament_size = 1
        
        currentL = []
        for seed in seed_scheduleL:
            currentL.append(constraints.constrain(Schedule.fromstring(out_func, seed, 'seed')))
        
        for indiv in next_generation(currentL, p, out_func, constraints, 0):
            print '-'*40
            print indiv.title()
            print indiv
        

    elif args[0] == 'autotune':
        if len(args) < 2:
            print >> sys.stderr, 'expected 2 arguments to autotune'
            sys.exit(1)
        filter_func_name = args[1]
        #(input, out_func, test_func, scope) = getattr(examples, examplename)()
        (input, out_func, test_func, scope) = resolve_filter_func(filter_func_name)()
        
        seed_scheduleL = []
#        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8).parallel(y_blurUInt16)\n' +
#                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        seed_scheduleL.append('')
        seed_scheduleL.append('blur_y_blurUInt16.root()\nblur_x_blurUInt16.root()')
        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8)\n' +
                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        evaluate = halide.filter_image(input, out_func, DEFAULT_IMAGE)
        evaluate()
        T0 = time.time()
        evaluate()
        T1 = time.time()
        print 'Time for default schedule: %.4f'%(T1-T0)
        
        p = AutotuneParams()
        #p.parallel_compile_nproc = 4
        exclude = []
        for key in scope:
            if key.startswith('input_clamped'):
                exclude.append(scope[key])
        constraints = Constraints(exclude)
        
        autotune(filter_func_name, p, constraints=constraints, seed_scheduleL=seed_scheduleL)
    elif args[0] in ['test_sched']:
        #(input, out_func, test_func) = examples.blur()
        pass
    elif args[0] == 'test_fromstring':
        examplename = 'blur'
        (input, out_func, test_func, scope) = getattr(examples, examplename)()
        s = Schedule.fromstring(out_func, 'blur_x_blur0.root().parallel(y)\nblur_y_blur0.root().parallel(y).vectorize(x,8)')
    elif args[0] in ['autotune_compile_child', 'autotune_run_child']:           # Child process for autotuner
        #def child_handler(signum, stack_frame):
        #    f = open('child_%d.txt'%random.randrange(1000**2),'wt')
        #    f.write('')
        #    f.close()
        #signal.signal(signal.SIGCHLD, child_handler)
        #time.sleep(1.0)
#        print time.time()
        rest = args[1:]
        if len(rest) != 6:
            raise ValueError('expected 6 args after autotune_child_process')
        (filter_func_name, schedule_str, in_image, trials, parent_pid, binary_file) = rest
        trials = int(trials)
        parent_pid = int(parent_pid)
        #os.kill(parent_pid, signal.SIGCONT)
        
        (input, out_func, evaluate_func, scope) = resolve_filter_func(filter_func_name)()
        schedule = Schedule.fromstring(out_func, schedule_str)
        constraints = Constraints()         # FIXME: Deal with Constraints() mess
        schedule.apply(constraints)

        if args[0] == 'autotune_compile_child':
            T0 = time.time()
            #out = open('out.sh','wt')
            #out.write(' '.join(rest))
            #out.close()
            out_func.compileToFile(binary_file)
            print 'Success %.4f' % (time.time()-T0)
            return
        elif args[0] == 'autotune_run_child':
            if random.random() < 0.9:
                print 'Success %.4f'%(1.0+random.random()*1e-4)
            else:
                print 'Failure'
            sys.exit(0)
        else:
            raise ValueError(args[0])
        
        #Tbegin_compile = time.time()
        #evaluate = halide.filter_image(input, out_func, in_image)
        #Tend_compile = time.time()
        #Tcompile = Tend_compile - Tbegin_compile
        #if AUTOTUNE_VERBOSE:
        #    print 'compiled in', Tcompile, repr(str(schedule))
        
        #T = []
        #for i in range(trials):
        #    T0 = time.time()
        #    Iout = evaluate()
        #    T1 = time.time()
        #    T.append(T1-T0)

        #T = min(T)
        #Trun = time.time()-Tend_compile
        
        #print 'Success', T, Tcompile, Trun


        
    else:
        raise NotImplementedError('%s not implemented'%args[0])
#    test_schedules()
    
if __name__ == '__main__':
    main()
    
