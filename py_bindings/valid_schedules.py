
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

def default_check(cls, L):
    def count(C):
        return sum([isinstance(x, C) for x in L])
    if len(L) == 0:
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
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
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
    
    def check(self, L):
        "Given list of Schedule fragments (applied to a function) returns True if valid else False."
        return default_check(self.__class__, L)

class FragmentVarMixin:
    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
#        print 'fragments', cls
        return cls(random.choice(vars)) if len(vars) else None #[cls(x) for x in vars]

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
            #print '* check_duplicates', cls, 'fail'
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
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
        allV = caller_vars(root_func, func)+extra_caller_vars
        return cls(random.choice(allV)) if len(allV) else None
        #return [cls(x) for x in ]
        
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
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
        #return [cls(x,reuse_outer=True,vars=vars)  for x in vars]
        return cls(random.choice(vars),reuse_outer=True,vars=vars) if len(vars) else None
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
        if random.random() < TILE_PROB_SQUARE:
            self.xsize = self.ysize = blocksize_random()
        else:
            self.xsize = blocksize_random()
            self.ysize = blocksize_random()
        #print 'randomize_const, tile, size=%d,%d' % (self.xsize, self.ysize)

    def check(self, L):
        return check_duplicates(self.__class__, L)

    @staticmethod
    def fromstring(xvar, yvar, xnewvar, ynewvar, xsize, ysize):
        return FragmentTile(xvar=xvar, yvar=yvar, xsize=int(xsize), ysize=int(ysize), xnewvar=xnewvar, ynewvar=ynewvar)

    @staticmethod
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
        if len(vars)-1 <= 0:
            return None
        i = random.randrange(len(vars)-1)
        return cls(vars[i],vars[i+1],vars=vars)
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
    def random_fragment(root_func, func, cls, vars, extra_caller_vars):
        #print 'fragments', root_func, func, cls, vars, extra_caller_vars
        n = permutation.factorial(len(vars))
        if n <= 1:
            return None
        i = random.randrange(1,n)
        return cls(vars=vars, idx=i)
        #return [cls(vars=vars, idx=i) for i in range(1,permutation.factorial(len(vars)))]     # TODO: Allow random generation so as to not loop over n!
    
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
        fragment = cls.random_fragment(root_func, func, cls, all_vars, extra_caller_vars)
        if fragment is not None: #len(fragments):
            return fragment
#    if len(fragments) == 0:    # empty fragments can happen legitimately for e.g. chunk of the root func
#        raise ValueError(('fragments is empty', cls, all_vars, func.name()))
#    fragment = random.choice(fragments)
#    return fragment

def schedules_depth(root_func, func, vars, depth=0, random=False, extra_caller_vars=[]):
    "Un-checked schedules (FragmentList instances) of exactly the specified depth for the given Func."
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
        for L in schedules_depth(root_func, func, vars, depth-1, random):
            #print 'schedules_depth recurses', L
            if not L.check():
                continue
            all_vars = list(vars)
            for fragment in L:
                all_vars.extend(fragment.new_vars())
            for cls in randomized(fragment_classes):
                #print 'all_vars', all_vars
                fragment = cls.random_fragment(root_func, func, cls, all_vars, extra_caller_vars)
                #for fragment in randomized(cls.fragments(root_func, func, cls, all_vars, extra_caller_vars)):
                    #print 'fragment', fragment
                #print '=>', fragment
                    #print '*', len(L), L
                if fragment is not None:
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
            #print 'schedules_depth returns', L
            if L.check():
                #print '  check'
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
        return '%02d_%03d'%(self.generation,self.index) if self.identity_str is None else self.identity_str
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

    def apply(self, constraints=None, verbose=False):   # Apply schedule
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
            while 1:
                try:
                    ans = schedules_func(root_func, f, min_depth, max_depth_sel, random=True, extra_caller_vars=extra_caller_vars).next()
                    break
                except StopIteration:
                    continue
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
    ans = set()
    for (name, g) in halide.all_funcs(root_func).items():
#        rhs_names = [x.name() for x in g.rhs().funcs()]
        rhs_names = [x.name() for x in g.rhs().transitiveFuncs()]
        if func_name in rhs_names:
            ans |= set(func_lhs_var_names(g))
            #return ans
            #print 'inside caller_vars', g.name(), func_name, ans, rhs_names
    return sorted(ans)
