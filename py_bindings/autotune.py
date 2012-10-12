
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
from valid_schedules import *

sys.path += ['../util']

LOG_SCHEDULES = True      # Log all tried schedules (Fail or Success) to a text file
LOG_SCHEDULE_FILENAME = 'log_schedule.txt'
AUTOTUNE_VERBOSE = False #True #False #True
DEFAULT_IMAGE = 'apollo3.png'

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
    
    # 'replace'  - Replace Func's schedule with a new random schedule
    # 'consts'   - Just modify constants when mutating
    # 'add'      - Add a single schedule macro to existing schedule
    # 'remove'   - Removing a single schedule macro from existing schedule
    # 'edit'     - Edit (replace) a single schedule macro within Func's schedule
    # 'template' - Replace Func's schedule with one sampled by autotune_template
    
    min_depth = 0
    max_depth = DEFAULT_MAX_DEPTH
    
    trials = 5                  # Timing runs per schedule
    generations = 10
    
    compile_timeout = 20.0 #15.0
    run_timeout_mul = 2.0 #3.0           # Fastest run time multiplied by this factor plus bias is cutoff
    run_timeout_bias = 5.0               # Run subprocess additional bias to allow tester process to start up and shut down
    run_timeout_default = 5.0       # Assumed 'fastest run time' before best run time is established
    run_save_timeout = 20.0             # Additional timeout if saving output png
    
    crossover_mutate_prob = 0.15     # Probability that mutate() is called after crossover
    crossover_random_prob = 0.1      # Probability that crossover is run with a randomly generated parent
    
    num_print = 10

    check_output = True
    
    cores = None   # Number of processes to use simultaneously for parallel compile (None defaults to number of virtual cores)

    tune_dir = None                 # Autotuning output directory or None to use a default directory
    tune_link = None                # Symlink (string) pointing to tune_dir (if available)
    
    in_image = os.path.join(os.path.dirname(os.path.abspath(__file__)), DEFAULT_IMAGE)      # Input image to test
    
    summary_file = 'summary.txt'
    
    def __init__(self, argd={}):
        for (key, value) in argd.items():
            if not hasattr(self, key):
                raise ValueError('unknown command-line switch %s'%key)
            if isinstance(getattr(self, key), str) or key in ['tune_dir', 'tune_link']:
                setattr(self, key, argd[key])
            else:
                setattr(self, key, float(argd[key]) if ('.' in value or isinstance(getattr(self, key), float)) else int(argd[key]))
#        print argd
#        raise ValueError(self.cores)
    
    def dict_prob_mutate(self):
        start = 'prob_mutate_'
        return dict([(key[len(start):], getattr(self, key)) for key in dir(self) if key.startswith(start)])

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
            ans.apply(constraints)       # Apply schedule to determine if crossover invalidated new variables that were referenced
        except NameError: #, halide.ScheduleError):
            continue

        return ans

def mutate(a, p, constraints):
    "Mutate existing schedule using AutotuneParams p."
    a0 = a
    a = copy.copy(a0)
    extra_caller_vars = []      # FIXME: Implement extra_caller_vars, important for chunk().
    dmutate0 = p.dict_prob_mutate()
    
    while True:
#        for name in a.d.keys():
#            if random.random() < p.mutation_rate:
        name = random.choice(a.d.keys())
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
            elif mode == 'template':
                s = autotune_template.sample(halide.func_varlist(a.d[name].func)) # TODO: Use parent variables if chunk...
                a.d[name] = FragmentList.fromstring(a.d[name].func, s)
                a.genomelog = 'replace_template(%s)'%a0.identity()
            else:
                raise ValueError('Unknown mutation mode %s'%mode)
        except MutateFailed:
            continue
            
        try:
            #print 'Mutated schedule:' + '\n' + '-'*40 + '\n' + str(a) + '\n' + '-' * 40 + '\n'
            a.apply(constraints)       # Apply schedule to determine if random_schedule() invalidated new variables that were referenced
        except NameError:#, halide.ScheduleError):
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
    
def next_generation(prevL, p, root_func, constraints, generation_idx, timeL):
    """"
    Get next generation using elitism/mutate/crossover/random.
    
    Here prevL is list of Schedule instances sorted by decreasing fitness, and p is AutotuneParams.
    """
    prevL = [prevL[i] for i in range(len(prevL)) if get_error_str(timeL[i]['time']) is None]
    
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
    
    return ans

class AutotuneTimer:
    compile_time = 0.0
    run_time = 0.0
    def __init__(self):
        self.start_time = time.time()
    
def time_generation(L, p, test_gen_func, timer, constraints, display_text='', save_output=False, ref_output=''):
    #T0 = time.time()
    #Tcompile = [0.0]
    #Trun = [0.0]
    def status_callback(msg):
        stats_str = 'compile time=%d secs, run time=%d secs, total=%d secs'%(timer.compile_time, timer.run_time, time.time()-timer.start_time)
        sys.stderr.write('\n'*100 + '%s (%s)\n  Tune dir: %s\n%s\n'%(msg,stats_str,p.tune_link + ' => %s'%p.tune_dir if p.tune_link else p.tune_dir, display_text))
        sys.stderr.flush()

    test_gen_iter = iter(test_gen_func(L, constraints, status_callback, timer, save_output, ref_output))
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

COMPILE_TIMEOUT = 10001.0
COMPILE_FAIL    = 10002.0
RUN_TIMEOUT     = 10003.0
RUN_FAIL        = 10004.0
RUN_CHECK_FAIL  = 10005.0

def get_error_str(timeval):
    "Get error string from special (high-valued) timing value (one of the above constants)."
    d = {COMPILE_TIMEOUT: 'COMPILE_TIMEOUT',
         COMPILE_FAIL:    'COMPILE_FAIL',
         RUN_TIMEOUT:     'RUN_TIMEOUT',
         RUN_FAIL:        'RUN_FAIL',
         RUN_CHECK_FAIL:  'RUN_CHECK_FAIL'}
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

def run_timeout(L, timeout, last_line=False, time_from_subproc=False, shell=False):
    """
    Run shell command in list form, e.g. L=['python', 'autotune.py'], using subprocess.
    
    Returns None on timeout otherwise str output of subprocess (if last_line just return the last line).
    
    If time_from_subproc is True then the starting time will be read from the first stdout line of the subprocess (seems to have a bug).
    """
    fout = tempfile.TemporaryFile()
    proc = subprocess.Popen(L, stdout=fout, stderr=fout, shell=shell)
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
        return -1, None

    fout.seek(0)
    ans = fout.read()
    if last_line:
        ans = ans.strip().split('\n')[-1].strip()
    return proc.returncode, ans

def default_tester(input, out_func, p, filter_func_name, allow_cache=True):
    cache = {}
    best_run_time = [p.run_timeout_default]

    nproc = p.cores
    if nproc is None:
        nproc = multiprocessing.cpu_count()
    
    #def signal_handler(signum, stack_frame):            # SIGCHLD is sent by default when child process terminates
    #    f = open('parent_%d.txt'%random.randrange(1000**2),'wt')
    #    f.write('')
    #    f.close()
    #signal.signal(signal.SIGCONT, signal_handler)
    
    def test_func(scheduleL, constraints, status_callback, timer, save_output=False, ref_output=''):       # FIXME: Handle constraints
        in_image = p.in_image
        def subprocess_args(schedule, schedule_str, compile=True):
            binary_file = os.path.join(p.tune_dir, 'f' + schedule.identity())
            mode_str = 'compile' if compile else 'run'
            
            sh_args = ['python', 'autotune.py', 'autotune_%s_child'%mode_str, filter_func_name, schedule_str, os.path.abspath(in_image), '%d'%p.trials, binary_file, str(int(save_output)), (ref_output if p.check_output else '')]
            sh_name = binary_file + '_' + mode_str + '.sh'
            with open(sh_name, 'wt') as sh_f:
                os.chmod(sh_name, 0755)
                sh_f.write(' '.join(sh_args[:4]) + ' "' + repr(sh_args[4])[1:-1] + '" ' + ' '.join(sh_args[5:-1]) + ' '  + ('"' + sh_args[-1] + '"' if p.check_output else '""') + '\n')
            return (sh_args, binary_file + '.png')
            
        # Compile all schedules in parallel
        compile_count = [0]
        lock = threading.Lock()
        def compile_schedule(i):
            status_callback('Compile %d/%d'%(compile_count[0]+1,len(scheduleL)))
            
            schedule = scheduleL[i]
            schedule_str = str(schedule)
            if schedule_str in cache:
                return cache[schedule_str]
            
            T0 = time.time()
            (argL, output) = subprocess_args(schedule, schedule_str, True)
            res,out = run_timeout(argL, p.compile_timeout, last_line=True)
            Tcompile = time.time()-T0
            
            timer.compile_time = timer_compile + time.time() - Tbegin_compile
            with lock:
                compile_count[0] += 1

            if out is None:
                return {'time': COMPILE_TIMEOUT, 'compile': Tcompile, 'run': 0.0, 'output': output}
            if not out.startswith('Success'):
                return {'time': COMPILE_FAIL, 'compile': Tcompile, 'run': 0.0, 'output': output}
            return {'time': 0.0, 'compile': Tcompile, 'run': 0.0, 'output': output}
        
        timer_compile = timer.compile_time
        Tbegin_compile = time.time()
        shuffled_idx = range(len(scheduleL))
        random.shuffle(shuffled_idx)
        compiledL = threadmap.map(compile_schedule, shuffled_idx, n=nproc)

        #Ttotal_compile = time.time()-Tbegin_compile
        
        assert len(compiledL) == len(scheduleL)
        
        # Run schedules in serial
        def run_schedule(i):
            status_callback('Run %d/%d'%(i+1,len(scheduleL)))
            schedule = scheduleL[i]

            schedule_str = str(schedule)
            if schedule_str in cache:
                return cache[schedule_str]

            compiled_ans = compiledL[i]
            if get_error_str(compiled_ans['time']) is not None:
                return compiled_ans
                
            T0 = time.time()
            #res,out = run_timeout(subprocess_args(schedule, schedule_str, False), best_run_time[0]*p.run_timeout_mul*p.trials+p.run_timeout_bias, last_line=True)
            (argL, output) = subprocess_args(schedule, schedule_str, False)
            res,out = autotune_child(argL[2:], best_run_time[0]*p.run_timeout_mul*p.trials+p.run_timeout_bias+(p.run_save_timeout if save_output else 0.0))
            Trun = time.time()-T0
            
            if out is None:
                ans = {'time': RUN_TIMEOUT, 'compile': compiled_ans['compile'], 'run': Trun, 'output': output}
            elif not out.startswith('Success') or len(out.split()) != 2:
                code = RUN_FAIL
                if out.startswith('RUN_CHECK_FAIL'):
                    code = RUN_CHECK_FAIL
                ans = {'time': code, 'compile': compiled_ans['compile'], 'run': Trun, 'output': output}
            else:
                T = float(out.split()[1])
                best_run_time[0] = min(best_run_time[0], T)
                ans = {'time': T, 'compile': compiled_ans['compile'], 'run': Trun, 'output': output}
                        
            timer.run_time = timer_run + time.time() - Tbegin_run
            
            return ans
        
        # Run, cache and display schedule in serial
        def run_full(i):
            ans = run_schedule(i)
            
            schedule = scheduleL[i]
            cache.setdefault(str(schedule), ans)

            e = get_error_str(ans['time'])
            first_part = 'Error %s'%e if e is not None else 'Best time %.4f'%ans['time']
            log_sched(p, schedule, '%s, compile=%.4f, run=%.4f'%(first_part, ans['compile'], ans['run']))
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

#def autotune(input, out_func, p, tester=default_tester, tester_kw={'in_image': 'lena_crop2.png'}):
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
    test_func = tester(input, out_func, p, filter_func_name)
    
    currentL = []
    for (iseed, seed) in enumerate(seed_scheduleL):
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, seed, 'seed(%d)'%iseed, 0, iseed)))
        #print 'seed_schedule new_vars', seed_schedule.new_vars()
#        currentL.append(seed_schedule)
#        currentL.append(Schedule.fromstring(out_func, ''))
#        currentL.
    if len(seed_scheduleL) == 0:
        currentL.append(constraints.constrain(Schedule.fromstring(out_func, '', 'seed(0)', 0, 0)))
    display_text = '\nTiming default schedules and obtaining reference output image\n'
    check_schedules(currentL)
    
    def format_time(timev):
        current_s = '%15.4f'%timev
        e = get_error_str(timev)
        if e is not None:
            current_s = '%15s'%e
        return current_s
        
    # Time default schedules and obtain reference output image
    timeL = time_generation(currentL, p, test_func, timer, constraints, display_text, True)
    ref_output = ''
    ref_log = '-'*40 + '\nDefault Schedules\n' + '-'*40 + '\n'
    for j in range(len(timeL)):
        timev = timeL[j]['time']
        current = currentL[j]
        current_output = timeL[j]['output']
        if os.path.exists(current_output):
            ref_output = current_output
        line_out = '%s %s'%(format_time(timev), repr(str(current)))
        print line_out
        ref_log += line_out + '\n'
    print
    log_sched(p, None, ref_log, filename=p.summary_file)
    
    if ref_output == '':
        raise ValueError('No reference output')
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
        
        timeL = time_generation(currentL, p, test_func, timer, constraints, display_text, ref_output=ref_output)
        
        bothL = sorted([(timeL[i]['time'], currentL[i]) for i in range(len(timeL))])
        display_text = '\n' + '-'*40 + '\n'
        display_text += 'Generation %d'%(gen) + '\n'
        display_text += '-'*40 + '\n'
        for (j, (timev, current)) in list(enumerate(bothL))[:p.num_print]:
            display_text += '%s %-4s %s' % (format_time(timev), current.identity(), repr(str(current))) + '\n'
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
        
        currentL = [x[1] for x in bothL]

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
    if len(rest) != 7:
        raise ValueError('expected 7 args after autotune_*_child')
    (filter_func_name, schedule_str, in_image, trials, binary_file, save_output, ref_output) = rest
    trials = int(trials)
    save_output = int(save_output)
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
    if args[0] == 'autotune_compile_child':
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
        subprocess.check_output(
            'cat %(func_name)s.bc | %(llvm_path)sopt -O3 | %(llvm_path)sllc -O3 -filetype=obj -o %(func_name)s.o' % locals(),
            shell=True
        )

        save_output_str = '-DSAVE_OUTPUT ' if save_output else ''
        #shutil.copyfile(default_runner, working)
        compile_command = 'g++ %(png_flags)s -DTEST_FUNC=%(func_name)s -DTEST_IN_T=%(in_t)s %(save_output_str)s-DTEST_OUT_T=%(out_t)s -I. -I%(support_include)s %(default_runner)s %(func_name)s.o -o %(func_name)s.exe'
        compile_command = compile_command % locals()
        print compile_command
        try:
            out = subprocess.check_output(compile_command, shell=True)
        except:
            raise ValueError('Compile failed:\n%s' % out)

        print 'Success %.4f' % (time.time()-T0)

        return

    elif args[0] == 'autotune_run_child':
        run_command = './%(func_name)s.exe %(trials)d %(in_image)s'
        run_command = run_command % locals()
        if ref_output != '':
            run_command += ' ' + ref_output
        print 'Testing: %s' % run_command

        # Don't bother running with timeout, parent process will manage that for us
        try:
            if timeout is None:
                out = subprocess.check_output(run_command, shell=True)
                print out.strip()
                return
            else:
                return run_timeout(run_command, timeout, last_line=True, shell=True)
        finally:
            os.chdir(orig)
    else:
        raise ValueError(args[0])

def parse_args():
    args = []
    d = {}
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg.startswith('-'):
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
    
def main():
    (args, argd) = parse_args()
    all_examples = 'blur dilate boxblur_cumsum boxblur_sat erode snake'.split() # local_laplacian'.split()
    if len(args) == 0:
        print 'autotune test|print|autotune examplename|test_sched|test_fromstring|test_variations'
        print 'autotune example [%s|all]'%('|'.join(all_examples))
        sys.exit(0)
    if args[0] == 'test':
        import autotune_test
        autotune_test.test()
    elif args[0] == 'example':
        if len(args) < 2:
            print >> sys.stderr, 'Expected 2 arguments'
            sys.exit(1)
        rest = sys.argv[3:]
        examplename = args[1]
        if examplename == 'snake':
            rest.extend(['-check_output', '0'])
        exampleL = all_examples if examplename == 'all' else [examplename]
        cores = multiprocessing.cpu_count()
        for examplename in exampleL:
            tune_dir = 'tune_%s'%examplename
            if os.path.exists(tune_dir):
                shutil.rmtree(tune_dir)
            system('HL_NUMTHREADS=%d python autotune.py autotune examples.%s.filter_func -cores %d -tune_dir "%s" %s' % (cores, examplename, cores if examplename != 'local_laplacian' else 2, tune_dir, ' '.join(rest)))
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
        (input, out_func, test_func, scope) = call_filter_func(filter_func_name)
        
        seed_scheduleL = []
#        seed_scheduleL.append('blur_y_blurUInt16.root().tile(x_blurUInt16, y_blurUInt16, _c0, _c1, 8, 8).vectorize(_c0, 8).parallel(y_blurUInt16)\n' +
#                              'blur_x_blurUInt16.chunk(x_blurUInt16).vectorize(x_blurUInt16, 8)')
        seed_scheduleL.append('')
        if filter_func_name == 'examples.snake.filter_func':        # Temporary workaround to test snake by injecting more valid schedules
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\nphi_clamped.root()\nphi_new.root()\nproxy_x0.root()\nproxy_y0.root()')
            seed_scheduleL.append('Dirac0.root()\nNx0.root()\nNy0.root()\ndistReg0.root()\ndx1.root()\ndx2.root()\ndx3.root()\ndx4.root()\ndx5.root()\ndy1.root()\ndy2.root()\ndy3.root()\ndy4.root()\ndy5.root()\ng_clamped.root()\nlap0.root()\nphi0.root()\nphi1.root()\nphi_clamped.root()')
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
        autotune_child(args)
    else:
        raise NotImplementedError('%s not implemented'%args[0])
#    test_schedules()
    
if __name__ == '__main__':
    main()
    
