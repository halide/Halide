
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
import threading
import shutil
import autotune_template
import psutil
import operator
from valid_schedules import *

sys.path += ['../util']

LOG_SCHEDULES = True      # Log all tried schedules (Fail or Success) to a text file
LOG_SCHEDULE_FILENAME = 'log_schedule.txt'
AUTOTUNE_VERBOSE = False #True #False #True
IMAGE_EXT = '.ppm'
DEFAULT_IMAGE = 'apollo3' + IMAGE_EXT
DEBUG_CHECKS = False

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
    population_size = 128 #64 #5 #32 #128
    
    # Different mutation modes (mutation is applied to each Func independently)
    prob_mutate_replace  = 0.2
    prob_mutate_consts   = 0.2
    prob_mutate_add      = 0.2
    prob_mutate_remove   = 0.2
    prob_mutate_edit     = 0.2
    prob_mutate_template = 1.0
    prob_mutate_copy     = 0.2
    prob_mutate_group    = 0.0      # Seems to converge to local minima -- do not use.
        
    # 'replace'  - Replace Func's schedule with a new random schedule
    # 'consts'   - Just modify constants when mutating
    # 'add'      - Add a single schedule macro to existing schedule
    # 'remove'   - Removing a single schedule macro from existing schedule
    # 'edit'     - Edit (replace) a single schedule macro within Func's schedule
    # 'template' - Replace Func's schedule with one sampled by autotune_template
    # 'copy'     - Replace Func's schedule with one randomly sampled from another Func
    # 'group'    - Randomly sample a Func from a group with nontrivial size and replace all funcs in the group with that schedule
    
    image_ext = IMAGE_EXT               # Image extension for reference outputs. Can be overridden by tune_image_ext special variable.

    min_depth = 0
    max_depth = DEFAULT_MAX_DEPTH
    
    trials = 5                  # Timing runs per schedule
    generations = 50
    
    group_generations = 0            # Iters to run with grouping constraints enabled (0 to not use grouping)
    
    compile_timeout = 20.0 #15.0        # Compile timeout in sec
    compile_memory_limit = 2500         # Compile memory limit in MB or None for no limit
    
    run_timeout_mul = 2.0 #3.0           # Fastest run time multiplied by this factor plus bias is cutoff
    run_timeout_bias = 5.0               # Run subprocess additional bias to allow tester process to start up and shut down
    run_timeout_default = 5.0       # Assumed 'fastest run time' before best run time is established
    run_save_timeout = 20.0             # Additional timeout if saving output png
    
    crossover_mutate_prob = 0.15     # Probability that mutate() is called after crossover
    crossover_random_prob = 0.1      # Probability that crossover is run with a randomly generated parent
    
    num_print = 10

    max_nontrivial = 7              # When generating random schedules, max number of nontrivial (non-root/inline) funcs
    
    check_output = True
    
    compile_threads = None          # Number of processes to use simultaneously for parallel compile (None defaults to number of virtual/hyperthreaded cores)
    hl_threads = None               # Passed in as HL_NUMTHREADS (None defaults to HL_NUMTHREADS if set or else number of virtual/hyperthreaded cores divided by 2)

    tune_dir = None                 # Autotuning output directory or None to use a default directory
    tune_link = None                # Symlink (string) pointing to tune_dir (if available)
    
    in_images = []                  # List of input images to test (can pass multiple images using -in_images a.png:b.png)
                                    # First image is run many times to yield a best timing
    
    seed_reasonable = True          # Whether to seed with reasonable schedules
    prob_reasonable = 0.5           # Probability to sample reasonable schedule when sampling random schedule
    
    summary_file = 'summary.txt'
    plot_file = 'plot.png'

    def __init__(self, argd={}):
        for (key, value) in argd.items():
            if not hasattr(self, key):
                raise ValueError('unknown command-line switch %s'%key)
            if key == 'in_images':
                self.in_images = value.split(':')
            elif isinstance(getattr(self, key), str) or key in ['tune_dir', 'tune_link']:
                setattr(self, key, argd[key])
            else:
                setattr(self, key, float(argd[key]) if ('.' in value or isinstance(getattr(self, key), float)) else int(argd[key]))
#        print argd
#        raise ValueError(self.cores)
        if len(self.in_images) == 0:
            self.in_images = [os.path.join(os.path.dirname(os.path.abspath(__file__)), x) for x in
                              [DEFAULT_IMAGE, 'coyote2' + IMAGE_EXT, 'bird' + IMAGE_EXT]]
        if self.compile_threads is None:
            self.compile_threads = multiprocessing.cpu_count()
        if self.hl_threads is None:
            if 'HL_NUMTHREADS' in os.environ:
                self.hl_threads = int(os.environ['HL_NUMTHREADS'])
            self.hl_threads = multiprocessing.cpu_count() / 2

    def dict_prob_mutate(self):
        start = 'prob_mutate_'
        return dict([(key[len(start):], getattr(self, key)) for key in dir(self) if key.startswith(start)])

def debug_check(f, info=None):
    if DEBUG_CHECKS:
        if not f.check(f):
            print str(f)
            print info
            raise ValueError
        g = Schedule.fromstring(f.root_func, str(f))
        if not g.check(g):
            print '-'*40
            print 'f:'
            print str(f)
            print '-'*40
            print 'g:'
            print str(g)
            print '-'*40
            print info
            raise ValueError

def subsample_join(L):
    idx = range(len(L))
    n = random.randrange(1,len(L)+1)
    idx = sorted(random.sample(idx, n))
    return ''.join([L[i] for i in idx])
    
def reasonable_schedule(root_func, chunk_cutoff=0, tile_prob=0.0, sample_fragments=False, schedule_args=()):
    """
    Get a reasonable schedule (like gcc's -O3) given a chunk cutoff (0 means never chunk).
    """
    ans = {}
    schedule = Schedule(root_func, ans, *schedule_args)
    def callback(f, fparent):
        #do_tile = tile_mode == 1
        #if tile_mode >= 2:
        #    do_tile = random.random() < 0.5
        do_tile = random.random() < tile_prob
            
        maxval = 1000
        if fparent is None:
            footprint = [maxval]
        else:
            footprint = list(fparent.footprint(f))
        footprint = [(x if x > 0 else maxval) for x in footprint]
        n = 128/f.rhs().type().bits   # Vectorize by 128-bit
        prob = random.random()
        #if fparent is not None:
        #    print f.name(), fparent.name(), footprint
        if reduce(operator.mul, footprint, 1) == 1:
            ans[f.name()] = FragmentList(f, [])
        elif all([x <= chunk_cutoff for x in footprint]):
            chunk_var = chunk_vars(schedule, f)[0]
            varlist = halide.func_varlist(f)
            if len(varlist) == 0:
                ans[f.name()] = FragmentList(f, [FragmentChunk(chunk_var)])
            else:
                x = varlist[0]
#                ans[f.name()] = FragmentList(f, [FragmentChunk(chunk_var), FragmentVectorize(x, n)])
                ans[f.name()] = FragmentList(f, [FragmentChunk(chunk_var)])
        else:
            varlist = halide.func_varlist(f)
            if len(varlist) == 0:
                s = '.root()'
            elif len(varlist) == 1:
                x = varlist[0]
                if do_tile:
                    if sample_fragments:
                        s = '.root()' + subsample_join(['.split(%(x)s,%(x)s,_c0,8)'%locals(),'.vectorize(_c0,%(n)d)'%locals()])
                        if '.split' not in s:
                            s = s.replace('vectorize(_c0', 'vectorize(x')
                    else:
                        s = '.root().split(%(x)s,%(x)s,_c0,8).vectorize(_c0,%(n)d)'%locals()
                else:
                    s = '.root().split(%(x)s,%(x)s,_c0,8)'%locals()
            else:
                x = varlist[0]
                y = varlist[1]
                if do_tile:
                    if sample_fragments:
                        s = '.root()' + subsample_join(['.tile(%(x)s,%(y)s,_c0,_c1,%(n)d,%(n)d)'%locals(),
                                                        '.vectorize(_c0,%(n)d)'%locals(),
                                                        '.parallel(%(y)s)' % locals()])
                        if '.tile' not in s:
                            s = s.replace('vectorize(_c0', 'vectorize(x')
                    else:
                        s = '.root().tile(%(x)s,%(y)s,_c0,_c1,%(n)d,%(n)d).vectorize(_c0,%(n)d).parallel(%(y)s)' % locals()
                else:
                    s = '.root().parallel(%(y)s)' % locals()
            ans[f.name()] = FragmentList.fromstring(f, s)

    halide.visit_funcs(root_func, callback)
    #sys.exit(1)
    
    return schedule

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
    a0 = a
    b0 = b
    while True:
        a = constraints.constrain(copy.copy(a0))
        b = constraints.constrain(copy.copy(b0))
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
        
        ans = Schedule(a.root_func, d)
        
        try:
            ans.apply(constraints, check=True)       # Apply schedule to determine if crossover invalidated new variables that were referenced
        except BadScheduleError: #, halide.ScheduleError):
            continue

        debug_check(ans)
            
        return ans

def mutate(a, p, constraints, grouping):
    "Mutate existing schedule using AutotuneParams p."
    a0 = a
    extra_caller_vars = []      # FIXME: Implement extra_caller_vars, important for chunk().
    dmutate0 = p.dict_prob_mutate()
    
    keys = a0.d.keys()
    if grouping is not None:
        keys = [group[0] for group in grouping]
    
    while True:
        a = copy.copy(a0)
#        for name in a.d.keys():
#            if random.random() < p.mutation_rate:
        name = random.choice(keys)
        dmutate = dict(dmutate0)
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
                a.d[name] = a.d[name].added(a.root_func, extra_caller_vars, a)
                a.genomelog = 'mutate_add(%s)'%a0.identity()
            elif mode == 'remove':
                assert len(a.d[name]) > p.min_depth
                #raise NotImplementedError
                a.d[name] = a.d[name].removed()
                a.genomelog = 'mutate_remove(%s)'%a0.identity()
            elif mode == 'edit':
#                        raise NotImplementedError
                a.d[name] = a.d[name].edited(a.root_func, extra_caller_vars, a)
                a.genomelog = 'mutate_edit(%s)'%a0.identity()
            elif mode == 'template':
                s = autotune_template.sample(halide.func_varlist(a.d[name].func)) # TODO: Use parent variables if chunk...
                a.d[name] = FragmentList.fromstring(a.d[name].func, s)
                a.genomelog = 'mutate_template(%s)'%a0.identity()
            elif mode == 'copy':
                chosen = random.choice(a.d.keys())
                a.d[name] = FragmentList(a.d[name].func, a.d[chosen])
                a.genomelog = 'mutate_copy(%s)'%a0.identity()
            elif mode == 'group':
                grouping0 = default_grouping(a0.root_func)
                nontrivial_groups = [group for group in grouping0 if len(group) > 1]
                if len(nontrivial_groups) == 0:
                    continue
                d_func = halide.all_funcs(a0.root_func)
                
                group = random.choice(nontrivial_groups)
                ichosen = random.randrange(len(group))
                for indiv in group:
                    a.d[indiv] = FragmentList(d_func[indiv], list(a0.d[group[ichosen]]))
                #return Schedule(schedule.root_func, ans)
                a.genomelog = 'mutate_group(%s)'%a0.identity()
            else:
                raise ValueError('Unknown mutation mode %s'%mode)
        except MutateFailed:
            continue
            
        try:
            #print 'Mutated schedule:' + '\n' + '-'*40 + '\n' + str(a) + '\n' + '-' * 40 + '\n'
            a.apply(constraints, check=True)       # Apply schedule to determine if random_schedule() invalidated new variables that were referenced
        except BadScheduleError:#, halide.ScheduleError):
            continue
            
        debug_check(a)
            
        return a

def random_or_reasonable(root_func, p, max_nontrivial, grouping):
    if random.random() < p.prob_reasonable:
        return reasonable_schedule(root_func, 0, random.random(), random.randrange(2), ('reasonable', -2, -2, 'reasonable'))
    else:
        return random_schedule(root_func, p.min_depth, p.max_depth, max_nontrivial=max_nontrivial, grouping=grouping)

def select_and_crossover(prevL, p, root_func, constraints, max_nontrivial, grouping):
    a = tournament_select(prevL, p, root_func, max_nontrivial, grouping)
    b = tournament_select(prevL, p, root_func, max_nontrivial, grouping)
    if random.random() < p.crossover_random_prob:
        #b = random_schedule(root_func, p.min_depth, p.max_depth, max_nontrivial=max_nontrivial, grouping=grouping)
        b = random_or_reasonable(root_func, p, max_nontrivial, grouping)
    c = crossover(a, b, constraints)
    is_mutated = False
    if random.random() < p.crossover_mutate_prob:
        c = mutate(c, p, constraints, grouping)
        is_mutated = True
    c.genomelog = 'crossover(%s, %s)'%(a.identity(), b.identity()) + ('' if not is_mutated else '+'+c.genomelog.replace('(-1_-1)', '()'))
    debug_check(c)
#    if is_mutated:
#        c.genomelog = 'XXXX'
#        c.identity_str = 'XXXXX'
    return c

def select_and_mutate(prevL, p, root_func, constraints, max_nontrivial, grouping):
    a = tournament_select(prevL, p, root_func, max_nontrivial, grouping)
    c = mutate(a, p, constraints, grouping)
    #c.genomelog = 'mutate(%s)'%a.identity()
    debug_check(c)
    return c

def tournament_select(prevL, p, root_func, max_nontrivial, grouping):
    i = random.randrange(p.tournament_size)
    if i >= len(prevL):
        #ans = random_schedule(root_func, p.min_depth, p.max_depth, max_nontrivial=max_nontrivial, grouping=grouping)
        ans = random_or_reasonable(root_func, p, max_nontrivial, grouping)
    else:
        ans = copy.copy(prevL[i])
    debug_check(ans)
    return ans
    
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
    
def default_grouping(root_func):
    def strip(name):
        return name.rstrip('0123456789')
        
    nameL = sorted(list(halide.all_funcs(root_func)))
    group = {}
    for name in sorted(nameL):
        group.setdefault(strip(name), []).append(name)
    return [x[1] for x in sorted(group.items())]
    
def is_grouping(p, generation_idx):
    return p.group_generations > 0 and generation_idx < p.group_generations
    
def apply_grouping(schedule, grouping):
    if grouping is not None:
        d_func = halide.all_funcs(schedule.root_func)
        ans = {}
        for group in grouping:
            for indiv in group:
                ans[indiv] = FragmentList(d_func[indiv], list(schedule.d[group[0]]))
        #return Schedule(schedule.root_func, ans)
        return Schedule(schedule.root_func, ans, schedule.genomelog, schedule.generation, schedule.index, schedule.identity_str)
    return schedule
    
def next_generation(prevL, p, root_func, constraints, generation_idx, timeL):
    """"
    Get next generation using elitism/mutate/crossover/random.
    
    Here prevL is list of Schedule instances sorted by decreasing fitness, and p is AutotuneParams.
    """
    assert len(prevL) == len(timeL)
    bothL = sorted([(timeL[i]['time'], prevL[i], timeL[i]) for i in range(len(timeL))])
    prevL = [x[1] for x in bothL]
    timeL = [x[2] for x in bothL]
    
    prevL = [prevL[i] for i in range(len(prevL)) if get_error_str(timeL[i]['time']) is None]
    
    grouping = None
    if is_grouping(p, generation_idx):
        grouping = default_grouping(root_func)

    ans = []
    schedule_strs = set()
    def append_unique(schedule, mode):
        schedule = apply_grouping(schedule, grouping)
        if not schedule.check(schedule):
            raise Duplicate
        s = str(schedule).strip()
        if s not in schedule_strs:
            schedule_strs.add(s)
            schedule.generation = generation_idx
            schedule.index = len(ans)
            schedule.identity_str = None
            debug_check(schedule)
            ans.append(schedule)
        else:
            raise Duplicate
    
    max_nontrivial = p.max_nontrivial
    if is_grouping(p, generation_idx):
        max_nontrivial = 1000**2
    
    def do_crossover():
        append_unique(constraints.constrain(select_and_crossover(prevL, p, root_func, constraints, max_nontrivial, grouping)), 'crossover')
    def do_mutated():
        append_unique(constraints.constrain(select_and_mutate(prevL, p, root_func, constraints, max_nontrivial, grouping)), 'mutated')
    def do_random():
#        append_unique(constraints.constrain(random_schedule(root_func, p.min_depth, p.max_depth, max_nontrivial=max_nontrivial, grouping=grouping)), 'random')
        append_unique(constraints.constrain(random_or_reasonable(root_func, p, max_nontrivial, grouping)), 'random')
    def do_until_success(func):
        while True:
            try:
                func()
                return
            except (Duplicate, BadScheduleError):
                continue

#    random_pct = 1-p.pop_mutated_pct-p.pop_crossover_pct-p.pop_elitism_pct

    for i in range(int(p.population_size*p.prob_pop['elitism'])):
        if i < len(prevL):
            current = copy.copy(prevL[i])
            if not '(elite copy of' in current.genomelog:
                current.genomelog += ' (elite copy of %s)' % current.identity() # FIXMEFIXME
            try:
                append_unique(current, 'elite')
            except Duplicate:
                pass
    
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
    
    return ans

class AutotuneTimer:
    compile_time = 0.0
    run_time = 0.0
    def __init__(self):
        self.start_time = time.time()
    
def time_generation(L, p, test_gen_func, timer, constraints, display_text='', save_output=False, compare_schedule=None, trials_override=None):
    #T0 = time.time()
    #Tcompile = [0.0]
    #Trun = [0.0]
    def status_callback(msg):
        stats_str = 'compile time=%d secs, run time=%d secs, total=%d secs, compile_threads=%d, hl_threads=%d'%(timer.compile_time, timer.run_time, time.time()-timer.start_time, p.compile_threads, p.hl_threads)
        sys.stderr.write('\n'*100 + '%s (%s)\n  Tune dir: %s\n%s\n'%(msg,stats_str,p.tune_link + ' => %s'%p.tune_dir if p.tune_link else p.tune_dir, display_text))
        sys.stderr.flush()

    test_gen_iter = iter(test_gen_func(L, constraints, status_callback, timer, save_output, compare_schedule, trials_override))
    def time_schedule():
        info = test_gen_iter.next() #test_func(current, constraints)()
        #print 'time_schedule', current, info
        #timer.compile_time += info['compile_avg']
        #timer.run_time += info['run']
        return info #['time']
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
        success += get_error_str(ans[-1]['time']) is None #< COMPILE_TIMEOUT
        timer.total_time = time.time()-timer.start_time
        #stats_str = '%.0f%% succeed, compile time=%d secs, run time=%d secs, total=%d secs' % (success*100.0/(i+1),timer.compile_time, timer.run_time, timer.total_time)
        if AUTOTUNE_VERBOSE:
            print '%.5f secs'%ans[-1]['time']
    #print 'Statistics: %s'%stats_str
    return ans

def log_sched(p, schedule, s, no_output=False, filename=LOG_SCHEDULE_FILENAME, f={}):
    if LOG_SCHEDULES:
        if filename not in f:
            f[filename] = open(os.path.join(p.tune_dir, filename),'wt')
        if no_output:
            return
        if schedule is not None:
            f[filename].write('-'*40 + '\n' + schedule.title() + '\n' + str(schedule) + '\n' + s + '\n')
        else:
            f[filename].write(s + '\n')
        f[filename].flush()

COMPILE_TIMEOUT  = 10001.0
COMPILE_FAIL     = 10002.0
COMPILE_MEMLIMIT = 10003.0
RUN_TIMEOUT      = 10004.0
RUN_FAIL         = 10005.0
RUN_CHECK_FAIL   = 10006.0

def get_error_str(timeval):
    "Get error string from special (high-valued) timing value (one of the above constants)."
    d = {COMPILE_TIMEOUT:  'COMPILE_TIMEOUT',
         COMPILE_FAIL:     'COMPILE_FAIL',
         COMPILE_MEMLIMIT: 'COMPILE_MEMLIMIT',
         RUN_TIMEOUT:      'RUN_TIMEOUT',
         RUN_FAIL:         'RUN_FAIL',
         RUN_CHECK_FAIL:   'RUN_CHECK_FAIL'}
    if timeval in d:
        return d[timeval]
    return None

SLEEP_TIME = 0.01

def wait_timeout(proc, timeout, T0=None, memory_limit=None):
    "Wait for subprocess to end (returns return code) or timeout (returns None)."
    if T0 is None:
        T0 = time.time()
    while True:
        p = proc.poll()
        if p is not None:
            return p
        if time.time() > T0+timeout:
            return RUN_LIMIT_TIMEOUT
        if memory_limit is not None and get_mem_recurse(proc.pid) > memory_limit:
            return RUN_LIMIT_MEMLIMIT
        time.sleep(SLEEP_TIME)

RUN_LIMIT_TIMEOUT = -2000
RUN_LIMIT_MEMLIMIT = -2001
RUN_LIMIT_ERRCODES = [RUN_LIMIT_TIMEOUT, RUN_LIMIT_MEMLIMIT]

def get_mem_recurse(pid):
    "Get memory in bytes used by pid."
    proc = psutil.Process(pid)
    #ans = proc.get_memory_info()[0]
    #print 'Memory used:', ans
    #return ans
    ans = 0
    for x in [proc] + proc.get_children(True):
        try:
            ans += x.get_memory_info()[0]
        except psutil.error.Error:
            pass
    #f = open('memused.txt','at')
    #f.write(str(ans) + '\n')
    #f.close()
    return ans
    #return sum(x.get_memory_info()[0] for x in L)

def kill_recursive(pid, timeout=1.0):
    proc = psutil.Process(pid)
    T0 = time.time()
    L = [proc] + proc.get_children(True)
    while len(L):
        Lnew = []
        for x in L:
            try:
                x.kill()
            except psutil.error.NoSuchProcess:
                pass
            except psutil.error.Error:
                Lnew.append(x)
        L = Lnew
        if time.time() > T0 + timeout:
            print 'Could not kill pid %d and children' % pid
            break
    
def run_limit(L, timeout, last_line=False, time_from_subproc=False, shell=False, memory_limit=None):
    """
    Run shell command in list form, e.g. L=['python', 'autotune.py'], using subprocess.
    
    Returns (status_code, str output of subprocess). If last_line just returns the last line.
    
    If timed out then status code is set to RUN_LIMIT_TIMEOUT.
    
    If memory_limit is not None then it should be a max RSS of the process and children in bytes. If the process exceeds this amount
    it will be killed and the status code set ot RUN_LIMIT_MEMLIMIT.
    """
    fout = tempfile.TemporaryFile()
    proc = subprocess.Popen(L, stdout=fout, stderr=fout, shell=shell)
    pid = proc.pid
    T0 = None
    if time_from_subproc:
        raise NotImplementedError
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
    status = wait_timeout(proc, timeout, T0, memory_limit)
    if status in RUN_LIMIT_ERRCODES:
        kill_recursive(proc.pid)
        return status, ''
        
    fout.seek(0)
    ans = fout.read()
    if last_line:
        ans = ans.strip().split('\n')[-1].strip()
    return proc.returncode, ans

def schedule_ref_output(p, schedule, j):
    return os.path.join(p.tune_dir, 'f' + schedule.identity() + '_%d'%j + p.image_ext)
    
def default_tester(input, out_func, p, filter_func_name, allow_cache=True):
    cache = {}
    best_run_time = [p.run_timeout_default]

    nproc = p.compile_threads
    hl_threads = p.hl_threads
        
    #def signal_handler(signum, stack_frame):            # SIGCHLD is sent by default when child process terminates
    #    f = open('parent_%d.txt'%random.randrange(1000**2),'wt')
    #    f.write('')
    #    f.close()
    #signal.signal(signal.SIGCONT, signal_handler)
    
    (input, out_func, evaluate_func, scope) = call_filter_func(filter_func_name)
    (out_w, out_h, out_channels) = scope.get('tune_out_dims', (-1, -1, -1))
    
    def test_func(scheduleL, constraints, status_callback, timer, save_output=False, compare_schedule=None, trials_override=None):       # FIXME: Handle constraints
        in_images = p.in_images
        assert len(in_images) > 0, 'No input images'
        do_check = False
        if p.check_output and compare_schedule is not None:
            do_check = True
            ref_output = [schedule_ref_output(p, compare_schedule, j) for j in range(len(in_images))]
            #assert len(ref_output) == len(in_images), (len(ref_output), len(in_images))
        
        def do_save_output(i):
            return save_output and i == 0
            
        def subprocess_args(i, schedule, schedule_str, compile=True, j=0, trials=None):
            if trials is None:
                if trials_override is not None:
                    trials = trials_override[i]
                else:
                    trials = p.trials
            binary_file = os.path.join(p.tune_dir, 'f' + schedule.identity())
            mode_str = 'compile' if compile else 'run'
            
            save_filename = ''
            if do_save_output(i):
                save_filename = schedule_ref_output(p, schedule, j)
                
            sh_args = ['HL_NUMTHREADS=%d'%hl_threads, 'python', 'autotune.py', 'autotune_%s_child'%mode_str, filter_func_name, schedule_str, os.path.abspath(in_images[j]), '%d'%trials, binary_file, save_filename, (ref_output[j] if do_check else ''), str(out_w), str(out_h), str(out_channels), str(hl_threads)]
            sh_line = (' '.join(sh_args[:5]) + ' "' + repr(sh_args[5])[1:-1] + '" ' + ' '.join(sh_args[6:9]) + ' '  +
                           ('"' + sh_args[9] + '"') + ' ' +
                           ('"' + sh_args[10] + '"' if p.check_output else '""') + ' ' + ' '.join(sh_args[11:]) + '\n')
            sh_name = binary_file + '_' + mode_str + '.sh'
            with open(sh_name, 'wt') as sh_f:
                os.chmod(sh_name, 0755)
                sh_f.write(sh_line)
            return (sh_args, sh_line, binary_file + p.image_ext)
            
        # Compile all schedules in parallel
        compile_count = [0]
        lock = threading.Lock()
        def compile_schedule(i):
            status_callback('Compile %d/%d'%(compile_count[0]+1,len(scheduleL)))
            
            schedule = scheduleL[i]
            schedule_str = str(schedule)

            (argL, arg_line, output) = subprocess_args(i, schedule, schedule_str, True)

            if schedule_str in cache:
                return cache[schedule_str]
            
            T0 = time.time()
            res,out = run_limit(arg_line, p.compile_timeout, last_line=True, memory_limit=p.compile_memory_limit*(1000**2), shell=True)
            Tcompile = time.time()-T0
            
            timer.compile_time = timer_compile + time.time() - Tbegin_compile
            with lock:
                compile_count[0] += 1

            if res == RUN_LIMIT_TIMEOUT:
                return {'time': COMPILE_TIMEOUT, 'compile': Tcompile, 'run': 0.0, 'output': output, 'compile_out': 'None'}
            if res == RUN_LIMIT_MEMLIMIT:
                return {'time': COMPILE_MEMLIMIT, 'compile': Tcompile, 'run': 0.0, 'output': output, 'compile_out': 'None'}
            if not out.startswith('Success'):
                return {'time': COMPILE_FAIL, 'compile': Tcompile, 'run': 0.0, 'output': output, 'compile_out': out}
            return {'time': 0.0, 'compile': Tcompile, 'run': 0.0, 'output': output, 'compile_out': out}
        
        timer_compile = timer.compile_time
        Tbegin_compile = time.time()
        shuffled_idx = range(len(scheduleL))
        #random.shuffle(shuffled_idx)       # Causes mismatches during run
        compiledL = threadmap.map(compile_schedule, shuffled_idx, n=nproc)

        #Ttotal_compile = time.time()-Tbegin_compile
        
        assert len(compiledL) == len(scheduleL)
        
        def max_run_time(trials):
            return best_run_time[0]*p.run_timeout_mul*p.trials+p.run_timeout_bias+(p.run_save_timeout if save_output else 0.0)
            
        # Run schedules in serial
        def run_schedule(i):
            status_callback('Run %d/%d'%(i+1,len(scheduleL)))
            schedule = scheduleL[i]
            schedule_str = str(schedule)
            
            T0 = time.time()

            def parse_out_error(out):
                if res == RUN_LIMIT_TIMEOUT:
                    return {'time': RUN_TIMEOUT, 'compile': compiled_ans['compile'], 'run': time.time()-T0, 'output': output, 'compile_out': compiled_ans['compile_out']}
                elif not out.startswith('Success') or len(out.split()) != 2:
                    code = RUN_FAIL
                    if out.startswith('RUN_CHECK_FAIL'):
                        code = RUN_CHECK_FAIL
                    return {'time': code, 'compile': compiled_ans['compile'], 'run': time.time()-T0, 'output': output, 'compile_out': compiled_ans['compile_out']}
                return None
                
            # Write (as a side-effect) the run script
            (argL, arg_line, output) = subprocess_args(i, schedule, schedule_str, False)

            if schedule_str in cache:
                return cache[schedule_str]

            compiled_ans = compiledL[i]
            if get_error_str(compiled_ans['time']) is not None:
                return compiled_ans

            # Check the list of input images against their reference outputs (if provided)
            if do_check or do_save_output(i):
                for j in range(1, len(in_images)):
                    (argL, arg_line, output) = subprocess_args(i, schedule, schedule_str, False, j, 1)
                    res,out = autotune_child(argL[3:], max_run_time(1))
                    ans = parse_out_error(out)
                    if ans is not None:
                        return ans
                
            #res,out = run_timeout(subprocess_args(schedule, schedule_str, False), best_run_time[0]*p.run_timeout_mul*p.trials+p.run_timeout_bias, last_line=True)
            (argL, arg_line, output) = subprocess_args(i, schedule, schedule_str, False)
            res,out = autotune_child(argL[3:], max_run_time(p.trials))
            
            ans = parse_out_error(out)
            if ans is None:
                T = float(out.split()[1])
                best_run_time[0] = min(best_run_time[0], T)
                ans = {'time': T, 'compile': compiled_ans['compile'], 'run': time.time()-T0, 'output': output, 'compile_out': compiled_ans['compile_out']}
                        
            timer.run_time = timer_run + time.time() - Tbegin_run
            
            return ans
        
        # Run, cache and display schedule in serial
        def run_full(i):
            ans = run_schedule(i)
            
            schedule = scheduleL[i]
            cache.setdefault(str(schedule), ans)

            e = get_error_str(ans['time'])
            first_part = 'Error %s'%e if e is not None else 'Best time %.6f'%ans['time']
            log_sched(p, schedule, '%s, compile=%.6f, run=%.6f, compile_out=%s'%(first_part, ans['compile'], ans['run'], ans['compile_out']))
            return ans
            
        Tbegin_run = time.time()
        timer_run = timer.run_time
        runL = map(run_full, range(len(scheduleL)))
        
#        for i in range(len(scheduleL)):
#            schedule = scheduleL[i]
            #runL[i]['compile_avg'] = Ttotal_compile/len(scheduleL)
        
        return runL
        
    return test_func
    

def check_schedules(currentL):
    "Verify that all schedules are valid (according to .check() rules at least)."
    for schedule in currentL:
        if not schedule.check():
            raise ValueError('schedule fails check: %s'%str(schedule))

#def autotune(input, out_func, p, tester=default_tester, tester_kw={'in_images': 'lena_crop2.png'}):
def call_filter_func(filter_func_name, cache={}):
    if not '(' in filter_func_name:         # Call the function if no parentheses (args) given
        filter_func_name += '()'

    if filter_func_name in cache:
        return cache[filter_func_name]
    if '.' in filter_func_name:
        before_paren = filter_func_name[:filter_func_name.index('(')]
        exec "import " + before_paren[:before_paren.rindex('.')]
    ans = eval(filter_func_name)
    cache[filter_func_name] = ans
    return ans

def autotune(filter_func_name, p, tester=default_tester, constraints=Constraints(), seed_scheduleL=[]):
    timer = AutotuneTimer()

    p = copy.deepcopy(p)
    if p.tune_dir is None:
        p.tune_dir = tempfile.mkdtemp('', 'tune_')
        #p.tune_dir = os.path.join(os.path.abspath('.'), 'tune')
    p.tune_dir = os.path.abspath(p.tune_dir)
    if not os.path.exists(p.tune_dir):
        os.makedirs(p.tune_dir)
        
    try:
        tune_link0 = '~/.tune'
        tune_link = os.path.expanduser(tune_link0)
        if os.path.exists(tune_link):
            os.remove(tune_link)
        os.symlink(p.tune_dir, tune_link)
        p.tune_link = tune_link0
    except:
        pass
        
    log_sched(p, None, None, no_output=True)    # Clear log file

    random.seed(0)
    (input, out_func, evaluate_func, scope) = call_filter_func(filter_func_name)
    if 'tune_in_images' in scope:
        p.in_images = scope['tune_in_images']
    if 'tune_image_ext' in scope:
        p.image_ext = scope['tune_image_ext']
    test_func = tester(input, out_func, p, filter_func_name)
    
    seed_scheduleL = list(seed_scheduleL)
        
    currentL = []
    for (iseed, seed) in enumerate(seed_scheduleL):
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, seed, 'seed(%d)'%iseed, 0, iseed)))

    if p.seed_reasonable:
        chunk_cutoff = 0
        for sample_fragments in [0, 1]:
            for tile_prob in numpy.arange(0.0,1.01,0.1): #chunk_cutoff in range(1,5):
                schedule_args = ('reasonable(%.1f,%d)'%(tile_prob,sample_fragments), 0, len(currentL))
                currentL.append(reasonable_schedule(out_func, chunk_cutoff, tile_prob, sample_fragments, schedule_args))

    if len(seed_scheduleL) == 0:
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, '', 'seed(0)', 0, 0)))

    nref = 0
    for (ref_name, ref_schedule_str) in scope.get('tune_ref_schedules', {}).items():
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, ref_schedule_str, ref_name, 0, len(currentL))))
        nref += 1

        #print 'seed_schedule new_vars', seed_schedule.new_vars()
#        currentL.append(seed_schedule)
#        currentL.append(Schedule.fromstring(out_func, ''))
#        currentL.
    display_text = '\nTiming reference schedules and obtaining reference output image\n'
    check_schedules(currentL[:len(currentL)-nref])
    
    def format_time(timev):
        current_s = '%17.6f'%timev
        e = get_error_str(timev)
        if e is not None:
            current_s = '%17s'%e
        return current_s
        
    trials_override = [p.trials] * (len(currentL)-nref) + [p.trials*2]*nref     # Obtain more accurate times for reference schedules
    # Time reference schedules and obtain reference output image for the first schedule
    timeL = time_generation(currentL, p, test_func, timer, constraints, display_text, True, trials_override=trials_override)
    #ref_output = ''
    ref_log = '-'*40 + '\nReference Schedules\n' + '-'*40 + '\n'
    compare_schedule = currentL[0]
    for j in range(len(timeL)):
        timev = timeL[j]['time']
        current = currentL[j]
        current_output = timeL[j]['output']
        #if os.path.exists(current_output):
        #    ref_output = current_output
        line_out = '%s %12s %s'%(format_time(timev), current.genomelog, current.oneline())
        print line_out
        ref_log += line_out + '\n'
    print
    log_sched(p, None, ref_log, filename=p.summary_file)
    
    # Keep only the seed schedules for the genetic algorithm
    assert len(currentL) == len(timeL)
    nseed = len(currentL)-nref
    currentL = currentL[:nseed]
    timeL = timeL[:nseed]
    #if ref_output == '':
    #    raise ValueError('No reference output')
#    timeL = time_generation(currentL, p, test_func, timer, constraints, display_text)
#    print timeL
#    sys.exit(1)
    
    for gen in range(1,p.generations+1):
        if gen == 1:
            display_text = '\nTiming generation %d'%gen

        currentL = next_generation(currentL, p, out_func, constraints, gen, timeL)
        # The (commented out) following line tests injecting a bad schedule for blur example (should fail with RUN_CHECK_FAIL).
        #currentL.append(constraints.constrain(Schedule.fromstring(out_func, 'blur_x_blurUInt16.chunk(x_blurUInt16)\nblur_y_blurUInt16.root().vectorize(x_blurUInt16,16)', 'bad_schedule', gen, len(currentL))))
        check_schedules(currentL)
        
        timeL = time_generation(currentL, p, test_func, timer, constraints, display_text, compare_schedule=compare_schedule)
        
        bothL = sorted([(timeL[i]['time'], currentL[i], timeL[i]) for i in range(len(timeL))])
        display_text = '\n' + '-'*40 + '\n'
        display_text += 'Generation %d'%(gen) + '\n'
        display_text += '-'*40 + '\n'
        for (j, (timev, current, time_dict)) in list(enumerate(bothL))[:p.num_print]:
            display_text += '%s %-4s %s' % (format_time(timev), current.identity(), current.oneline()) + '\n'
        display_text += '\n'

        success_count = 0
        for timev in timeL:
            e = get_error_str(timev['time'])
            if e is None:
                success_count += 1
                
        display_text += ' '*16 + '%d/%d succeed (%.0f%%)\n' % (success_count, len(timeL), success_count*100.0/len(timeL))
        print display_text
        log_sched(p, None, display_text, filename=p.summary_file)
        sys.stdout.flush()
        #autotune_plot.main((os.path.join(p.tune_dir, p.summary_file), os.path.join(p.tune_dir, p.plot_file)))
        os.system('python autotune_plot.py "%s" "%s"' % (os.path.join(p.tune_dir, p.summary_file), os.path.join(p.tune_dir, p.plot_file)))

import inspect
_scriptfile = inspect.getfile(inspect.currentframe()) # script filename (usually with path)
_scriptpath = os.path.dirname(os.path.abspath(_scriptfile)) # script directory

def _ctype_of_type(t):
    if t.isFloat():
        assert t.bits in (32, 64)
        if t.bits == 32:
            return 'float'
        else:
            return 'double'
    else:
        width = str(t.bits)
        ty = ''
        if t.isInt():
            ty = 'int'
        else:
            assert(t.isUInt())
            ty = 'uint'
        return ty + width + '_t'

def autotune_child(args, timeout=None):
    rest = args[1:]
    if len(rest) != 11:
        raise ValueError('expected 11 args after autotune_*_child')
    (filter_func_name, schedule_str, in_image, trials, binary_file, save_filename, ref_output, out_w, out_h, out_channels, hl_threads) = rest

    trials = int(trials)
    #save_output = int(save_output)
    out_w = int(out_w)
    out_h = int(out_h)
    out_channels = int(out_channels)
    hl_threads = int(hl_threads)
    #os.kill(parent_pid, signal.SIGCONT)
    
    (input, out_func, evaluate_func, scope) = call_filter_func(filter_func_name)
    schedule = Schedule.fromstring(out_func, schedule_str.replace('\\n', '\n'))
    constraints = Constraints()         # FIXME: Deal with Constraints() mess
    schedule.apply(constraints)

    llvm_path = os.path.abspath('../llvm/Release+Asserts/bin/')
    if not llvm_path.endswith('/'):
        llvm_path += '/'

    # binary_file: full path
    orig = os.getcwd()
    func_name = os.path.basename(binary_file)
    working = os.path.dirname(binary_file)
    os.chdir(working)
    if args[0] in ['autotune_compile_child', 'autotune_compile_run_child']:
        T0 = time.time()
        print "In %s, compiling %s" % (working, func_name)

        # emit object file
        out_func.compileToFile(func_name)

        # copy default_runner locally
        default_runner = os.path.join(_scriptpath, 'default_runner', 'default_runner.cpp')
        support_include = os.path.join(_scriptpath, '../support')

        in_t  = _ctype_of_type(input.type())
        out_t = _ctype_of_type(out_func.returnType())

        # get PNG flags
        png_flags = subprocess.check_output('libpng-config --cflags --ldflags', shell=True).replace('\n', ' ')
        
        # assemble bitcode
        def check_output(s):
            print s
            subprocess.check_output(s,shell=True)
            
        check_output(
            'cat %(func_name)s.bc | %(llvm_path)sopt -O3 -always-inline | %(llvm_path)sllc -O3 -filetype=obj -o %(func_name)s.o' % locals()
        )

        #save_output_str = '-DSAVE_OUTPUT ' if save_output else ''
        #shutil.copyfile(default_runner, working)
        compile_command = 'g++ -DTEST_FUNC=%(func_name)s -DTEST_IN_T=%(in_t)s -DTEST_OUT_T=%(out_t)s -I. -I%(support_include)s %(default_runner)s %(func_name)s.o -o %(func_name)s.exe -lpthread %(png_flags)s'
        compile_command = compile_command % locals()
        print compile_command
        try:
            out = subprocess.check_output(compile_command, shell=True)
        except:
            raise ValueError('Compile failed:\n%s' % out)

        print 'Success %.6f' % (time.time()-T0)

        #return

    if args[0] in ['autotune_run_child', 'autotune_compile_run_child']:
        run_command = 'HL_NUMTHREADS=%(hl_threads)s ./%(func_name)s.exe %(trials)d %(in_image)s "%(ref_output)s" %(out_w)d %(out_h)d %(out_channels)d "%(save_filename)s"'
        run_command = run_command % locals()
        print 'Testing: %s' % run_command

        # Don't bother running with timeout, parent process will manage that for us
        try:
            if timeout is None:
                out = subprocess.check_output(run_command, shell=True)
                print out.strip()
                return
            else:
                return run_limit(run_command, timeout, last_line=True, shell=True)
        finally:
            os.chdir(orig)
    #else:
    #    raise ValueError(args[0])

def is_number(s):
    try:
        float(s)
        return True
    except:
        return False
        
def parse_args():
    args = []
    d = {}
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg.startswith('-') and not is_number(arg):
            if i == len(argv)-1:
                raise ValueError('dashed argument at end of arg list: %s'%arg)
            d[arg[1:]] = argv[i+1]
            i += 2
            continue
        else:
            args.append(arg)
        i += 1
    return (args, d)

def system(s):
    print s
    os.system(s)
    
def print_tunables(f):
    print 'Tunables:'
    for (fname, f) in sorted(halide.all_funcs(f).items()):
        print fname, ' '.join(x for x in halide.func_varlist(f))
#        sys.stdout.write(fname + '.root()\\n')
    print

def root_all_str(f):
    "String for root all schedule."
    ans = []
    for (fname, f) in sorted(halide.all_funcs(f).items()):
#        print fname, ' '.join(x for x in halide.func_varlist(f))
        ans.append(fname + '.root()\n')
    return ''.join(ans)

def test():
    import autotune_test
    autotune_test.test()
    test_valid_schedules()
    
def main():
    (args, argd) = parse_args()
    all_examples = 'blur dilate boxblur_cumsum boxblur_sat erode snake interpolate bilateral_grid camera_pipe local_laplacian'.split() # local_laplacian'.split()
    if len(args) == 0:
        print 'autotune test|print|autotune examplename|test_sched|test_fromstring|test_variations'
        print 'autotune example [%s|all]'%('|'.join(all_examples))
        sys.exit(0)
    if args[0] == 'test':
        test()
    elif args[0] == 'example':
        if len(args) < 2:
            print >> sys.stderr, 'Expected 2 arguments'
            sys.exit(1)
        rest = sys.argv[3:]
        examplename = args[1]
        #if examplename in ['snake', 'bilateral_grid', 'camera_pipe']:
        #    rest.extend(['-check_output', '0'])
        exampleL = all_examples if examplename == 'all' else [examplename]
        if examplename.lower().endswith('.txt'):
            exampleL = open(examplename,'rt').read().strip().split()
        for examplename in exampleL:
            tune_dir = 'tune_%s'%examplename
            if os.path.exists(tune_dir):
                shutil.rmtree(tune_dir)
            if examplename == 'local_laplacian':
                compile_threads = 2
                if multiprocessing.cpu_count() >= 32:
                    compile_threads = 8
                rest = ('-compile_threads %d -compile_timeout 120.0 -generations 200'%compile_threads).split() + rest
            elif examplename == 'interpolate':
                rest = '-generations 150 -compile_timeout 120.0'.split() + rest
            elif examplename in ['camera_pipe', 'bilateral_grid']:
                rest = '-generations 150'.split() + rest
            system('python autotune.py autotune examples.%s.filter_func -tune_dir "%s" %s' % (examplename, tune_dir, ' '.join(rest)))
    elif args[0] == 'test_random':
        import autotune_test
        global use_random_blocksize
        use_random_blocksize = False
        autotune_test.test_schedules(True, test_random=True)
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
        (input, out_func, test_func, scope) = call_filter_func(filter_func_name)

        seed_scheduleL = []
        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8)\n' +
                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        p = AutotuneParams()
        exclude = []
        #for key in scope:
        #    if key.startswith('input_clamped'):
        #        exclude.append(scope[key])
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
        (input, out_func, test_func, scope) = call_filter_func(filter_func_name)
        
        seed_scheduleL = []
#        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8).parallel(y_blurUInt16)\n' +
#                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        seed_scheduleL.append('')
        if filter_func_name == 'examples.snake.filter_func' and 0:        # Temporary workaround to test snake by injecting more valid schedules
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()')
        elif filter_func_name == 'examples.camera_pipe.filter_func' and 0:    # Workaround for camera_pipe
            seed_scheduleL = []
            """
            # Below schedule is totally reasonable but fails (compiler bug)
            seed_scheduleL.append('b_b.root()\nb_gb.root()\nb_gr.root()\nb_r.root()\ncorrected.root()\ncurve.root()\ncurved.root()\ndeinterleaved.root()\ndemosaic.root()\ndenoised.root()\ng_b.root()\ng_gb.root()\ng_gr.root()\ng_r.root()\ninterleave_x1.root()\ninterleave_x2.root()\ninterleave_x3.root()\ninterleave_x4.root()\ninterleave_x5.root()\ninterleave_x6.root()\ninterleave_y1.root()\ninterleave_y2.root()\ninterleave_y3.root()\nmatrix.root()\nmatrix_3200.root()\nmatrix_7200.root()\nprocessed.root()\nr_b.root()\nr_gb.root()\nr_gr.root()\nr_r.root()\nshifted.root()')
            """
            #seed_scheduleL.append('b_b.root()\nb_gb.root()\nb_gr.root()\nb_r.root()\ncorrected.root()\ncurve.root()\ncurved.root()\ndeinterleaved.root()\ndemosaic.root()\ndenoised.root()\ninterleave_x1.root()\ninterleave_x2.root()\ninterleave_x3.root()\ninterleave_x4.root()\ninterleave_x5.root()\ninterleave_x6.root()\ninterleave_y1.root()\ninterleave_y2.root()\ninterleave_y3.root()\nmatrix.root()\nmatrix_3200.root()\nmatrix_7200.root()\nprocessed.root()\nr_b.root()\nr_gb.root()\nr_gr.root()\nr_r.root()\nshifted.root()')
            seed_scheduleL.append('b_b.root()\nb_gb.root()\nb_gr.root()\nb_r.root()\ncorrected.root()\ncurve.root()\ncurved.root()\ndeinterleaved.root()\ndemosaic.root()\ndenoised.root()\ng_b.root()\ng_gb.root()\ng_gr.root()\ng_r.root()\ninterleave_x1.root()\ninterleave_x2.root()\ninterleave_x3.root()\ninterleave_x4.root()\ninterleave_x5.root()\ninterleave_x6.root()\ninterleave_y1.root()\ninterleave_y2.root()\ninterleave_y3.root()\nr_b.root()\nr_gb.root()\nr_gr.root()\nr_r.root()\nshifted.root()')
            seed_scheduleL.append('b_b.root()\nb_gb.root()\nb_gr.root()\nb_r.root()\ncorrected.root()\ncurve.root()\ncurved.root()\ndeinterleaved.root()\ndemosaic.root()\ndenoised.root()\ng_b.root()\ng_gb.root()\ninterleave_x3.root()\ninterleave_x4.root()\ninterleave_x5.root()\ninterleave_x6.root()\ninterleave_y1.root()\ninterleave_y2.root()\ninterleave_y3.root()\nr_b.root()\nr_gb.root()\nr_gr.root()\nr_r.root()\nshifted.root()')
            seed_scheduleL.append('b_b.root()\nb_gb.root()\nb_gr.root()\nb_r.root()\ncorrected.root()\ncurve.root()\ncurved.root()\ndeinterleaved.root()\ndemosaic.root()\ndenoised.root()\ng_b.root()\ng_gb.root()\ng_gr.root()\ng_r.root()\ninterleave_x1.root()\ninterleave_x2.root()\ninterleave_x3.root()\ninterleave_x4.root()\ninterleave_x5.root()\ninterleave_x6.root()\ninterleave_y1.root()\ninterleave_y2.root()\ninterleave_y3.root()\nr_b.root()\nshifted.root()')
        elif filter_func_name in ['examples.local_laplacian.filter_func', 'examples.bilateral_grid.filter_func'] or 1:
            seed_scheduleL = []
            seed_scheduleL.append(root_all_str(out_func)) #'clamped.root()\ncolor.root()\ndownx0.root()\ndownx1.root()\ndownx2.root()\ndownx3.root()\ndownx4.root()\ndownx5.root()\ndowny0.root()\ndowny1.root()\ndowny2.root()\ndowny3.root()\ndowny4.root()\ndowny5.root()\nfloating.root()\ngPyramid0.root()\ngPyramid1.root()\ngPyramid2.root()\ngPyramid3.root()\ngray.root()\ninGPyramid1.root()\ninGPyramid2.root()\ninGPyramid3.root()\nlPyramid0.root()\nlPyramid1.root()\nlPyramid2.root()\noutGPyramid0.root()\noutGPyramid1.root()\noutGPyramid2.root()\noutLPyramid0.root()\noutLPyramid1.root()\noutLPyramid2.root()\noutLPyramid3.root()\noutput.root()\nremap.root()\nupx0.root()\nupx1.root()\nupx2.root()\nupx3.root()\nupx4.root()\nupx5.root()\nupy0.root()\nupy1.root()\nupy2.root()\nupy3.root()\nupy4.root()\nupy5.root()')
            #seed_scheduleL.append('clamped.root()\ncolor.root()\ndownx0.root()\ndownx1.root()\ndowny0.root()\ndowny1.root()\nfloating.root()\ngPyramid0.root()\ngPyramid1.root()\ngray.root()\ninGPyramid1.root()\nlPyramid0.root()\noutGPyramid0.root()\noutLPyramid0.root()\noutLPyramid1.root()\noutput.root()\nremap.root()\nupx0.root()\nupx1.root()\nupy0.root()\nupy1.root()')
            #seed_scheduleL = []
            #seed_scheduleL.append('clamped.root()\ncolor.root()\ndownx0.root()\ndownx1.root()\ndownx10.root()\ndownx11.root()\ndownx12.root()\ndownx13.root()\ndownx2.root()\ndownx3.root()\ndownx4.root()\ndownx5.root()\ndownx6.root()\ndownx7.root()\ndownx8.root()\ndownx9.root()\ndowny0.root()\ndowny1.root()\ndowny10.root()\ndowny11.root()\ndowny12.root()\ndowny13.root()\ndowny2.root()\ndowny3.root()\ndowny4.root()\ndowny5.root()\ndowny6.root()\ndowny7.root()\ndowny8.root()\ndowny9.root()\nfloating.root()\ngPyramid0.root()\ngPyramid1.root()\ngPyramid2.root()\ngPyramid3.root()\ngPyramid4.root()\ngPyramid5.root()\ngPyramid6.root()\ngPyramid7.root()\ngray.root()\ninGPyramid1.root()\ninGPyramid2.root()\ninGPyramid3.root()\ninGPyramid4.root()\ninGPyramid5.root()\ninGPyramid6.root()\ninGPyramid7.root()\nlPyramid0.root()\nlPyramid1.root()\nlPyramid2.root()\nlPyramid3.root()\nlPyramid4.root()\nlPyramid5.root()\nlPyramid6.root()\noutGPyramid0.root()\noutGPyramid1.root()\noutGPyramid2.root()\noutGPyramid3.root()\noutGPyramid4.root()\noutGPyramid5.root()\noutGPyramid6.root()\noutLPyramid0.root()\noutLPyramid1.root()\noutLPyramid2.root()\noutLPyramid3.root()\noutLPyramid4.root()\noutLPyramid5.root()\noutLPyramid6.root()\noutLPyramid7.root()\noutput.root()\nremap.root()\nupx0.root()\nupx1.root()\nupx10.root()\nupx11.root()\nupx12.root()\nupx13.root()\nupx2.root()\nupx3.root()\nupx4.root()\nupx5.root()\nupx6.root()\nupx7.root()\nupx8.root()\nupx9.root()\nupy0.root()\nupy1.root()\nupy10.root()\nupy11.root()\nupy12.root()\nupy13.root()\nupy2.root()\nupy3.root()\nupy4.root()\nupy5.root()\nupy6.root()\nupy7.root()\nupy8.root()\nupy9.root()') #FIXMEFIXME
        #seed_scheduleL.append('blur_y_blurUInt16.root()\nblur_x_blurUInt16.root()')
        #seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8)\n' +
        #                      'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        #evaluate = halide.filter_image(input, out_func, DEFAULT_IMAGE)
        #evaluate()
        #T0 = time.time()
        #evaluate()
        #T1 = time.time()
        #print 'Time for default schedule: %.4f'%(T1-T0)
        p = AutotuneParams(argd)

        #p.parallel_compile_nproc = 4
        exclude = []
        #for key in scope:
        #    if key.startswith('input_clamped'):
        #        exclude.append(scope[key])
        constraints = Constraints(exclude)

        autotune(filter_func_name, p, constraints=constraints, seed_scheduleL=seed_scheduleL)
    elif args[0] in ['test_sched']:
        #(input, out_func, test_func) = examples.blur()
        pass
    elif args[0] == 'test_fromstring':
        examplename = 'blur'
        (input, out_func, test_func, scope) = getattr(examples, examplename)()
        s = Schedule.fromstring(out_func, 'blur_x_blur0.root().parallel(y)\nblur_y_blur0.root().parallel(y).vectorize(x,8)')
    elif args[0] in ['autotune_compile_child', 'autotune_run_child', 'autotune_compile_run_child']:           # Child process for autotuner
        autotune_child(args)
    else:
        raise NotImplementedError('%s not implemented'%args[0])
#    test_schedules()
    
if __name__ == '__main__':
    main()
    
