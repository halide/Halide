
# TODO: Include new added split() or tile() vars in reorder() list

import halide
import random
import copy
import permutation
import numpy
import itertools
import sys
random_module = random

DEFAULT_MAX_DEPTH = 4
FORCE_TILE = False
MUTATE_TRIES = 10
TILE_PROB_SQUARE = 0.5              # Probability of selecting square tile size (e.g. 8x8).

# --------------------------------------------------------------------------------------------------------------
# Valid Schedule Enumeration
# --------------------------------------------------------------------------------------------------------------

def default_check(cls, L, func):
    assert func is not None
    def count(C):
        return sum([isinstance(x, C) for x in L])
    if len(L) == 0:
        #if func.isReduction():
        #    return False
        return True         # Inline
    else:
        # Handle singleton fragments
        if count(FragmentVectorize) > 1:        # Allow at most one vectorize per Func schedule (FragmentList)
            #print '  * check ', cls, 'vectorize fail'
            return False
        if FORCE_TILE and (len(L) >= 2 and count(FragmentTile) < 1):
            #print '  * check ', cls, 'tile fail'
            return False
        root_count = count(FragmentRoot)
        chunk_count = count(FragmentChunk)
        if isinstance(L[0], FragmentRoot) and root_count == 1 and chunk_count == 0:
            return True
        elif isinstance(L[0], FragmentChunk) and chunk_count == 1 and root_count == 0:
            return True
    #print '  * check ', cls, 'generic fail'
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
            #print '* check_duplicates', cls, 'fail'
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
    def __str__(self):
        return '.unroll(%s,%d)'%(self.var,self.value)

class FragmentChunk(Fragment):
    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars, partial_schedule):
        #allV = caller_vars(root_func, func)+extra_caller_vars
        allV = chunk_vars(partial_schedule, func)
        return cls(random.choice(allV)) if len(allV) else None
        #return [cls(x) for x in ]
        
    def check(self, L, partial_schedule=None, func=None, vars=None):
        if partial_schedule is not None:
            if self.var not in chunk_vars(partial_schedule, func):
                return False
        return check_duplicates(self.__class__, L, func)

    def __str__(self):
        return '.chunk(%s)'%self.var

    def var_order(self, prev_order):
        raise ValueError('var_order called on FragmentChunk()')

# FragmentUpdate is just a stub class for now -- not used in tuning, just for comparing with human reference schedules
class FragmentUpdate(Fragment):
    def __str__(self):
        return '.update()'

    def var_order(self, prev_order):
        raise ValueError('var_order called on FragmentUpdate()')

for _cls in [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentUpdate]:
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
        
class FragmentTile(FragmentBlocksizeMixin,Fragment):
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

    def check(self, L, partial_schedule=None, func=None, vars=None):
        if vars is not None and (self.xvar not in vars or self.yvar not in vars):
            return False
        return check_duplicates(self.__class__, L, func)

    @staticmethod
    def fromstring(xvar, yvar, xnewvar, ynewvar, xsize, ysize):
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

    def new_vars(self):
        return [self.xnewvar, self.ynewvar]
    
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
    def fromstring(*L):
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
                    return False
        if not default_check(self.__class__, L, func):
            return False
        return [isinstance(x, FragmentReorder) for x in L] == [0]*(len(L)-1)+[1]
    
    def __str__(self):
        #ans = ''
        #assert len(self.pairwise_swaps) >= 1
        #for (a, b) in self.pairwise_swaps:
        #    ans += '.transpose(%s,%s)'%(a,b)
        #return ans
        return '.reorder(' + ','.join(v for v in self.permutation) + ')'

    def var_order(self, prev_order):
        orig_order = list(reversed(prev_order))      # Argument order (reversed)
        order = list(orig_order)
        try:
            indices = sorted([order.index(self.permutation[j]) for j in range(len(self.permutation))])
        except ValueError:
            raise BadScheduleError
        for (iperm, i) in enumerate(indices):
            order[i] = self.permutation[iperm]

        sub = [order[indices[j]] for j in range(len(indices))]
        if sub != list(self.permutation):
            print 'sub', sub
            print 'permutation', self.permutation
            print 'orig_order', orig_order
            print 'order', order
            raise ValueError

        return order



fragment_classes = [FragmentRoot, FragmentVectorize, FragmentParallel, FragmentUnroll, FragmentChunk, FragmentSplit, FragmentTile, FragmentReorder]
fragment_map = {'root': FragmentRoot,
                'vectorize': FragmentVectorize,
                'parallel': FragmentParallel,
                'unroll': FragmentUnroll,
                'chunk': FragmentChunk,
                'split': FragmentSplit,
                'tile': FragmentTile,
                'reorder': FragmentReorder,
                'update': FragmentUpdate}

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
            ans.append(fragment_fromstring(part))
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
        try:
            for x in self:
                if not x.check(self, partial_schedule, self.func, vars):
                    return False
                vars = sorted(set(vars + x.new_vars()))
        except BadScheduleError:
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
        cls = random.choice(fragment_classes)
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
            for cls in randomized(fragment_classes):
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
        maxlen = 100
        if len(ans) > maxlen:
            ans = ans[:maxlen-3] + '...'
        return "'" + ans + "'"

    def check(self, partial_schedule=None):
        #if set(self.d.keys()) != set(halide.all_funcs(self.root_func)):
        #    print sorted(self.d.keys())
        #    print sorted(halide.all_funcs(self.root_func))
        #    raise ValueError
        root_func_name = self.root_func.name()
        if root_func_name in self.d:
            L = self.d[root_func_name]
            if len(L) < 1 or not isinstance(L[0], FragmentRoot):
                return False
        else:
            return False
        #print 'check', self
        for x in self.d.values():
            if not x.check(partial_schedule):
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

    def apply(self, constraints=None, verbose=False, check=False):   # Apply schedule
        if check:
            if not self.check(self):
                raise BadScheduleError
        #return
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
            if constraints is not None and name in constraints.exclude_names:
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
                try:
                    exec s in scope
                except NameError:
                    raise BadScheduleError
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
    def fromstring(root_func, s, genomelog='', generation=-1, index=-1, fix=True):
        """
        Constructor from a string s such as 'f.root().parallel(y)\ng.chunk(y)' (same format as returned by str() method).
        
        If fix is True then tries to auto-fix human schedules into the strict internal schedule format, which requires:
         - Every func begins with its caller schedule e.g. f.root().parallel(y) is valid but f.parallel(y) is invalid
         - Reduction functions rf to be scheduled explicitly as either rf.root()|rf.chunk().
         - Output func g to be scheduled for its call schedule explicitly as g.root()...
        """
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
        

def random_schedule(root_func, min_depth=0, max_depth=DEFAULT_MAX_DEPTH, vars=None, constraints={}):
    """
    Generate Schedule for all functions called by root_func (recursively). Same arguments as schedules_func().
    """
    if vars is None:
        vars = halide.func_varlist(root_func)
    
    while 1:
        d_new_vars = {}
        schedule = {}
        schedule_obj = Schedule(root_func, schedule, 'random', -2, -2, 'random')
        
        def callback(f, parent):
            extra_caller_vars = d_new_vars.get(parent.name() if parent is not None else None,[])
    #        print 'schedule', f.name(), extra_caller_vars
    #        ans = schedules_func(root_func, f, min_depth, max_depth, random=True, extra_caller_vars=extra_caller_vars, vars=vars).next()
            name = f.name()
            if name in constraints:
                schedule[name] = constraints[name]
            else:
                max_depth_sel = max_depth # if f.name() != 'f' else 0
                while 1:
                    try:
                        ans = schedules_func(root_func, f, min_depth, max_depth_sel, random=True, extra_caller_vars=extra_caller_vars, partial_schedule=schedule_obj).next()
                        break
                    except StopIteration:
                        continue
                schedule[name] = ans
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

def caller_vars(root_func, func):
    "Given a root Func and current function return list of variables of the caller."
    func_name = func.name()
    ans = set()
    for (name, g) in halide.all_funcs(root_func).items():
#        rhs_names = [x.name() for x in g.rhs().funcs()]
        rhs_names = [x.name() for x in g.rhs().transitiveFuncs()]
        if func_name in rhs_names:
            ans |= set(func_lhs_var_names(g))
            #return ans
            #print 'inside caller_vars', g.name(), func_name, ans, rhs_names
    return sorted(ans)

def dfs(edges, start):
    G = {}
    for (a, b) in edges:
        G.setdefault(a, []).append(b)
        
    visited = set()
    def f(current):
        visited.add(current)
        for x in G.get(current, []):
            if x not in visited:
                f(x)
    f(start)
    return visited

def test_dfs():
    assert dfs([(1,2),(2,3)],1) == set([1, 2, 3])
    assert dfs([(1,2),(2,3),(4,5)],1) == set([1, 2, 3])
    assert dfs([(1,2),(2,3),(4,5),(1,5)],1) == set([1, 2, 3, 5])
    assert dfs([(1,2),(2,3),(4,5),(1,5),(5,6)],1) == set([1, 2, 3, 5, 6])
    assert dfs([(1,2),(2,3),(4,5),(1,5),(5,6),(4,5)],1) == set([1, 2, 3, 5, 6])
    assert dfs([(1,2),(2,3),(4,5),(5,6),(3,4)],1) == set([1, 2, 3, 4, 5, 6])
    print 'valid_schedules.dfs:        OK'

def callers(root_func):
    "Returns dict mapping func name f => list of all func names calling f."
    d = {}
    def callback(f, fparent):
        d.setdefault(f.name(), [])
        if fparent is not None:
            d[f.name()].append(fparent.name())
    halide.visit_funcs(root_func, callback, all_calls=True)
    return d

def toposort(data):
    data = dict((a, set(b)) for (a, b) in data.items())
    
    for k, v in data.items():
        v.discard(k)
        
    extras = reduce(set.union, data.values(), set()) - set(data.keys())
    data.update({x:set() for x in extras})
    
    ans = []
    while True:
        ordered = set(x for (x,dep) in data.items() if not dep)
        if len(ordered) == 0:
            break
        ans.extend(sorted(ordered))
        data = {x: (dep - ordered) for (x,dep) in data.items() if x not in ordered}
    return ans
    
def test_toposort():
    h2 = simple_program()
    d = callers(h2)

    #chunk_vars(Schedule.fromstring(h2,''), h2)

    assert toposort(d) == ['c_valid_h2', 'c_valid_h', 'c_valid_g', 'c_valid_f']
    
    f = halide.Func('c_valid2_f')
    g = halide.Func('c_valid2_g')
    root = halide.Func('c_valid2_root')
    h1 = halide.Func('c_valid2_h1')
    h2 = halide.Func('c_valid2_h2')
    h3 = halide.Func('c_valid2_h3')
    x, y = halide.Var('c_valid2_x'), halide.Var('c_valid2_y')

    f[x,y]=x+y
    g[x,y]=f[x,y]
    h1[x,y]=f[x,y]+f[x,y]
    h2[x,y]=h1[x,y]
    h3[x,y]=h2[x,y]
    root[x,y]=g[x,y]+h3[x,y]+g[x,y]

    #chunk_vars(Schedule.fromstring(root,''), root)
    
    assert toposort(callers(root)) == ['c_valid2_root', 'c_valid2_g', 'c_valid2_h3', 'c_valid2_h2', 'c_valid2_h1', 'c_valid2_f']
    assert toposort(callers(f)) == ['c_valid2_f']
    assert toposort(callers(g)) == ['c_valid2_g', 'c_valid2_f']
    
    print 'valid_schedules.toposort:   OK'

def chunk_vars(schedule, func, remove_inline=False):
    d_stack = {}      # Map f name to stack (loop ordering) of variable names
    d_callers = callers(schedule.root_func)
    d_func = halide.all_funcs(schedule.root_func)
    
    for fname in toposort(d_callers):
        if len(d_callers[fname]) == 0:                      # Root function (no callers) is implicitly root()
            fragment = schedule.d[fname] if fname in schedule.d else FragmentList(d_func[fname], [])
            d_stack[fname] = fragment.var_order()
        else:
            stackL = []                         # Allowed chunk variables of each caller-callee pair (intersect these)
            for fparent in d_callers[fname]:
                #print fname, fparent
                assert fparent in d_stack
                stack = list(d_stack[fparent])
                #print '  parent stack:', stack
                #L = schedule.d[fparent] if fparent in schedule.d else FragmentList(d_func[fparent], [])
                L = schedule.d[fname] if fname in schedule.d else FragmentList(d_func[fname], [])
                if len(L) == 0:
                    #print '  inline, no stack changes'
                    pass        # No stack changes
                elif isinstance(L[0], FragmentRoot):
                    stack = L.var_order()
                    #print '  root, stack order=', stack
                elif isinstance(L[0], FragmentChunk):
                    #print '  chunk'
                    #print '  schedule', repr(schedule)
                    #print '  L', L
                    order = list(reversed(halide.func_varlist(L.func)))         # FIXME: can reorder() reorder the caller variables?
                    #print '  initial order', order
                    for fragment in L[1:]:
                        order = fragment.var_order(order)
                        #print '  update order', order
                    try:
                        i = len(stack)-1 - stack[::-1].index(L[0].var)
                    except ValueError:
                        raise BadScheduleError
                    stack = stack[:i+1]
                    stack.extend(order)
                    #print '  result stack', stack
                else:
                    raise ValueError((fname, fparent, L[0]))
                stackL.append(set(stack))
            d_stack[fname] = list(stackL[0]) #sorted(reduce(set.intersection, stackL))        # FIXME: Not clear how to intersect two stacks...
            #print 'final stack:', d_stack[fname]
            #print '-'*20
            #print
        if fname == func.name():
            return sorted(set(d_stack[fname]))
    raise ValueError
        #stack[fname] = 1
        #print 'callers', fname, d_callers[fname]
        # Need to implement: reorder(), split(), tile(), stack popping
        
    
# Bad algorithm for determining chunk vars
def chunk_vars_removed(schedule, func, remove_inline=False):
    "Given partially completed schedule and func, return list of var names that func can chunk over."
    if remove_inline:
        schedule.d = dict(x for x in schedule.d.items() if len(x[0]))
#    print schedule.d
    ans = set([])

    edges = set()
    d_func = {}
    
    def callback(f, fparent):
        f_name = f.name()
        d_func[f_name] = f
        if fparent is None:
            return
        fparent_name = fparent.name()
        if f_name not in schedule.d:            # Implicitly inline
            edges.add((f_name, fparent_name))
        else:
            L = schedule.d[f_name]
            if len(L) == 0 or isinstance(L[0], FragmentChunk):
                edges.add((f_name, fparent_name))

    root_func_name = schedule.root_func.name()
    halide.visit_funcs(schedule.root_func, callback)
    #print 'edges', edges
    #print 'dfs', dfs(edges, func.name())
    reachable = dfs(edges, func.name()) - set([func.name()])
    #print 'reachable', reachable
    
    for rfunc in reachable:
        if rfunc == root_func_name:     # Root func is implicitly root()ed.
            ans |= set(halide.func_varlist(schedule.root_func))
        if rfunc in schedule.d:          
            L = schedule.d[rfunc]
            if len(L) >= 1 and isinstance(L[0], FragmentRoot):
                ans |= set(L.all_vars())
    
    return list(sorted(ans))

def simple_program(cache=[]):
    if len(cache):
        return cache[0]
    f = halide.Func('c_valid_f')
    g = halide.Func('c_valid_g')
    h = halide.Func('c_valid_h')
    h2 = halide.Func('c_valid_h2')
    x, y = halide.Var('c_valid_x'), halide.Var('c_valid_y')

    # f callers: g, h2
    # g callers: h, h2
    # h callers: h2
    # h2 callers: []
    f[x,y]=x+y
    g[x,y]=f[x,y]+f[x,y]
    h[x,y]=g[x,y]
    h2[x,y]=g[x,y]+f[x,y]+h[x,y]+g[x,y]
    
    cache.append(h2)
    return h2
    
def test_callers():
    h2 = simple_program()
    
    def filtname(s):
        return s[len('c_valid_'):]
        
    d = dict((filtname(x), sorted([filtname(z) for z in y])) for (x, y) in callers(h2).items())
    assert d == {'h': ['h2'], 'g': ['h', 'h2'], 'f': ['g', 'h2'], 'h2': []}

    print 'valid_schedules.callers:    OK'
    
def test_chunk_vars():
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
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().split(valid_x,valid_x,_c0,8)\nvalid_g.chunk(valid_y)'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root()'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root().tile(valid_x,valid_y,_c2,_c3,8,8)'), f, remove_inline) == ['_c2', '_c3', 'valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, 'valid_h.root().tile(valid_x,valid_y,_c0,_c1,8,8)\nvalid_g.root().parallel(valid_y)'), f, remove_inline) == ['valid_x', 'valid_y']
        assert chunk_vars(Schedule.fromstring(h, ''), f, remove_inline) == ['valid_x', 'valid_y']

    # None of these schedules should pass
    from examples.boxblur_cumsum import filter_func
    (input, out_func, evaluate, scope) = filter_func()
    L = ['output.root()\n\nsum_clamped.root().unroll(x,4).tile(x,y,_c0,_c1,64,16).split(_c1,_c1,_c2,4)\nsumx.chunk(_c1).reorder(c,x,y)\nweight.root()',
         'output.root().parallel(x)\n\nsum_clamped.chunk(c).split(y,y,_c0,16).parallel(c)\nsumx.chunk(_c0).tile(y,c,_c0,_c1,8,8).split(x,x,_c2,32)\nweight.root()',
         'output.root()\n\nsum_clamped.root().vectorize(y,8).tile(x,y,_c0,_c1,4,4).tile(_c0,_c1,_c2,_c3,32,32)\nsumx.chunk(_c2).vectorize(x,4).tile(x,y,_c0,_c1,16,16).unroll(y,4)\nweight.chunk(x).tile(x,y,_c0,_c1,64,64).tile(x,y,_c2,_c3,4,4)',
         'output.root().tile(x,y,_c0,_c1,16,2).split(_c1,_c1,_c2,4).parallel(c)\nsum.chunk(_c2).unroll(x,8).parallel(x)\nsum_clamped.chunk(y)\nsumx.chunk(y)\nweight.root().vectorize(y,2)',
         'output.root().tile(x,y,_c0,_c1,64,64).tile(x,y,_c2,_c3,2,2).unroll(y,64)\n\nsum_clamped.chunk(_c2).split(c,c,_c0,16).vectorize(_c0,4).reorder(_c0,c,y,x)\nsumx.chunk(_c0)\n',
         'output.root().parallel(y).vectorize(x,16)\nsum.chunk(x).vectorize(c,16)\nsum_clamped.chunk(x).tile(x,y,_c0,_c1,8,2).parallel(x)\nsumx.chunk(_c0).vectorize(c,8).unroll(y,2).unroll(x,32)\nweight.root().parallel(y).vectorize(y,2).parallel(x)',
         'output.root()\n\nsum_clamped.root().split(x,x,_c0,32)\nsumx.chunk(_c0).tile(x,y,_c0,_c1,64,64)\nweight.root().tile(x,y,_c0,_c1,8,8).parallel(x)',
         'output.root()\n\nsum_clamped.root().split(x,x,_c0,4).tile(_c0,y,_c1,_c2,64,32)\nsumx.chunk(_c2).tile(x,y,_c0,_c1,4,4)\nweight.chunk(x).unroll(x,2)',
         'output.root().parallel(y).split(x,x,_c0,8).tile(y,c,_c1,_c2,2,2)\n\nsum_clamped.chunk(_c2).unroll(c,8).vectorize(c,4).unroll(y,2)\nsumx.chunk(_c2)\nweight.chunk(_c2).tile(x,y,_c0,_c1,64,64).split(x,x,_c2,16).reorder(_c1,x,y,_c0)',
         'output.root().tile(x,y,_c0,_c1,4,4).split(_c0,_c0,_c2,8).split(x,x,_c3,8)\n\nsum_clamped.chunk(_c0)\nsumx.chunk(_c3).unroll(x,64).split(x,x,_c0,8).tile(y,c,_c1,_c2,16,2)\nweight.chunk(x).vectorize(x,2)',
         'output.root().split(y,y,_c0,2)\n\nsum_clamped.chunk(y).split(c,c,_c0,2).vectorize(_c0,16).parallel(y)\nsumx.chunk(_c0).split(c,c,_c0,16).unroll(x,64)\nweight.chunk(x).vectorize(x,8)']
    L = [Schedule.fromstring(out_func, x) for x in L]
#    n = sum([x.check(x) for x in L])
#    assert n == 0, n
    errL = []
    for (i, x) in enumerate(L):
        if x.check(x):
            errL.append(repr((i, x)))

    """
    schedule = L[5]
    print '='*80
    print 'sumx chunk vars:'
    print chunk_vars(schedule, halide.all_funcs(schedule.root_func)['sumx'])
    print
    print '='*80
    """
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

    print 'valid_schedules.chunk_vars: OK'

def test_valid_schedules():
    test_dfs()
    test_callers()
    test_toposort()
    test_chunk_vars()
    
if __name__ == '__main__':
    test_valid_schedules()
