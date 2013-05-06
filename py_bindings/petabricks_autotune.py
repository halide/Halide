# Halide autotuning with PetaBricks
# Rules: Use at most one parallel, and at the end.

import sys; sys.path += ['../../petabricks/scripts/']

from highlevelconfig import *
import candidatetester
from candidatetester import CandidateTester, CrashException
from sgatuner import Population
from tunerconfig import config
from tunerwarnings import InitialProgramCrash,ExistingProgramCrash,NewProgramCrash,TargetNotMet
import pbutil
import operator
import warnings
import sgatuner
from configtool import ConfigFile
from storagedirs import timers
import numpy
import signal
import psutil
import halide
import traceback
from ForkedWatchdog import Watchdog
from permutation import *

verbose = True #False
TIMELIMIT = 10.0        # Max time per compile or run
NUMTESTS = 2

#from sgatuner import *
#import highlevelconfig
#import sgatuner

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
#
# unroll 2+
# vectorize up to 32

FUNC_MAX      = 2
VAR_MAX       = 4

class HalideConfigAccessor(ConfigFile):          # Allows setting of keys without calling .add() first
    def __init__(self, src=None):
        if src is None:
            self.values = {}
        else:
            self.values = src.values         
        
    def __setitem__(self, k, v):
        if not k in self.values:
            self.add(k, v)
        else:
            ConfigFile.__setitem__(self, k, v)

class FuncSchedule(Item):
    def __init__(self, info, name, vars):
        self.vars = vars
        Item.__init__(self, info, name)

    def copyValues(self, src, dst):
        if src is dst:
            return
        for t in src.values: #self.tunables():
            dst[t] = src[t]

    def subname(self, sub):
        return self.name + '_' + sub
        
    def randomize(self, cfg, n):
        cfg = HalideConfigAccessor(cfg)
        func = cfg[self.subname('func')] = random.randrange(FUNC_MAX)
        if func == FUNC_ROOT:
            pass
        elif func == FUNC_INLINE:
            pass
        elif func == FUNC_CHUNK:
            raise NotImplementedError
        else:
            raise ValueError('Unknown func schedule %d' % func)
        
        for i in range(len(self.vars)):
            cfg[self.subname('var%d_arg0'%i)] = 0
            
            var = cfg[self.subname('var%d'%i)] = random.randrange(VAR_MAX) #VAR_PARALLEL if i == 1 else VAR_SERIAL #
            if var == VAR_VECTORIZE:
                cfg[self.subname('var%d_arg0'%i)] = random.choice([2, 4, 8, 16, 32])
            elif var == VAR_UNROLL:
                cfg[self.subname('var%d_arg0'%i)] = random.choice([2, 4, 8, 16, 32, 64])
            elif var in [VAR_SERIAL, VAR_PARALLEL]:
                pass
            elif schedule in [VAR_TILE, VAR_SPLIT]:
                pass
            else:
                raise ValueError('Unknown var schedule %d' % var)

        cfg[self.subname('transpose')] = (0 if random.random() <= (2.0/3.0) else random.randrange(1,factorial(len(self.vars))))
        
    def str(self, cfg):
        func = cfg[self.subname('func')]
        if func == FUNC_ROOT:
            ans = '%s.root()' % self.name
        elif func == FUNC_INLINE:
            return '' #'%s.inline()' % self.name
        else:
            raise ValueError('Unknown func schedule %d' % func)
        
        for i in range(len(self.vars)):
            var = cfg[self.subname('var%d'%i)]
            if var == VAR_SERIAL:
                pass
            elif var in [VAR_VECTORIZE, VAR_UNROLL]:
                ans += '.%s(%s, %d)' % ('vectorize' if var == VAR_VECTORIZE else 'unroll', self.vars[i], cfg[self.subname('var%d_arg0'%i)])
            elif var == VAR_PARALLEL:
                ans += '.parallel(%s)' % (self.vars[i])
            else:
                raise ValueError('Unknown var schedule %d' % var)
        
        transpose = cfg[self.subname('transpose')]
        if transpose > 0:
            #print transpose, self.vars, permutation(self.vars, transpose)
            #assert self.vars == ['x', 'y'], self.vars
            for (a, b) in pairwise_swaps(self.vars, permutation(self.vars, transpose)):
                ans += '.transpose(%s,%s)' % (a, b)
        return ans + ';'

class HalideHighLevelConfig(HighLevelConfig):
    def __init__(self, info, func_var_list):
        self.items = []
        
        for (func, vars) in func_var_list:
            self.items.append(FuncSchedule(info, func, vars)) #, 0, SCHEDULE_MAX))
    
    def randomize(self, cfg, n):
        for item in self.items:
            item.randomize(cfg, n)
            
    def str(self, cfg):
        return '\n'.join([x.str(cfg) for x in self.items])

class TimingRunTimeout(Exception):
    pass

class TimingRunFailed(Exception):
    pass
    
def runCommand(cmd, cfg, hl_cfg, limit, test, func_d, func, scope, cache={}):
    print 'runcommand ---------------------------------------------------------'
#    sys.exit(0)
    
    numtests = NUMTESTS
    
    schedule_str = hl_cfg.str(cfg)
    if schedule_str in cache:
        return cache[schedule_str]
    #print 'run |%s|, %s' % (cmd, ' '.join(str(x) for x in cfg.values.items()))
    if verbose:
        print '----------------------------------------------------------------'
        print 'run', cmd
        print schedule_str
    #timefile = '_time_out.txt'
    #schedulefile = '_schedule.h'
    
    #if os.path.exists(timefile):
    #    os.remove(timefile)
    #f = open(schedulefile, 'wt')
    #f.write(schedule_str)
    #f.close()
    #return pbutil.executeRun(['python', 'halide_schedule.py', '~schedule.txt']+cmd, config.metrics)
    #status = system_timelimit(('python halide_schedule.py %s %s %s %d' % (schedulefile, cmd, timefile, 1)), limit if limit is not None else 10*60.0)
    limit_t = limit if limit is not None else 10*60.0

    for value in func_d.values():
        value.reset()
    for line in schedule_str.split('\n'):
        #(funcname, rest) = line.split('.',1)
        #line = 'func_d["%s"].' % funcname + rest
        print 'exec |%s|' % line
        exec line in scope

    #"""
    halide.exit_on_signal()
    
    try:
        with Watchdog(limit_t):
            func.compileJIT()
    except Watchdog:
        raise TimingRunTimeout
    #"""
    """
    try:
        func.compileJIT()
    except:
        traceback.print_exc()
        raise TimingRunFailed
    """
    #if status == TIMEOUT:
    #    raise TimingRunTimeout
    Lans = []
    for i in range(numtests):
        #status = system_timelimit(('python halide_schedule.py %s %s %s %d' % (schedulefile, cmd, timefile, 0)), limit if limit is not None else 10*60.0)
        #if status == TIMEOUT:
        #    raise TimingRunTimeout
        #"""
        try:
            with Watchdog(limit_t):
                ans = test(func)
        except Watchdog:
            raise TimingRunTimeout
        #"""
        #ans = test(func)
        assert isinstance(ans, (float, int))
        #try:
        #    f = open(timefile, 'rt')
        #except:
        #    raise TimingRunFailed
        #s = f.read()
        #print s
        #if 'HL_NUMTHREADS not defined.' in s:
        #    raise ValueError('HL_NUMTHREADS not defined')

        #L = s.strip().split('\n')
        #f.close()
        
        #try:
        #    #assert len(L) >= 1
        #    ans = float(L[-1])
        #except:
        #    raise TimingRunFailed
        Lans.append(ans)
    
    if verbose:
        print '   => %f secs, sigma=%f' % (numpy.mean(Lans), numpy.std(Lans))
        print '----------------------------------------------------------------'
        print 
    cache[schedule_str] = Lans
    return Lans


import subprocess, threading

TIMEOUT = -100

devnull = open(os.devnull, 'wt')

def kill_recursive(pid, max_depth=5):
    for x in psutil.process_iter():
        try:
            kill = x.pid == pid
            current = x
            depth = 0
            while current is not None and depth <= max_depth:
                current = current.parent
                if current.pid == pid:
                    kill = True
                depth += 1
            #print x.pid, kill
            if kill:
                x.kill()
        except:
            continue
    #print 'done kill_recursive'
            
def system_timelimit(cmd, timeout):
    process = []
    def f():
#        process.append(subprocess.Popen(cmd, shell=False))
        process.append(subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull))
        process[0].communicate()
    thread = threading.Thread(target=f)
    thread.start()
    thread.join(timeout)
    if thread.is_alive() and len(process):
        #process[0].kill()
        pid = process[0].pid
        #print 'killing %d (and children) with SIGKILL' % pid
        kill_recursive(pid) #, signal.SIGKILL)
        thread.join()
        return TIMEOUT
    return process[0].returncode if len(process) else -200

class HalideCandidateTester: #(CandidateTester):
    def __init__(self, hl_cfg, app, n, test_func, func_d, func, scope):
        self.hl_cfg = hl_cfg
        self.app = app
        self.n = n + config.offset
        self.timeoutCount = 0
        self.crashCount = 0
        self.testCount = 0
        self.test_func = test_func
        self.func_d = func_d
        self.func = func
        self.scope = scope
        
    def nextTester(self):
        return self
        
    def cleanup(self):
        pass
        
    def test(self, candidate, limit=None):
        try:
            limit = TIMELIMIT
            
            self.testCount += 1
            cfgfile = candidate.cfgfile()
            testNumber = candidate.numTests(self.n)
            if testNumber>=config.max_trials:
                warnings.warn(tunerwarnings.TooManyTrials(testNumber+1))
    #    cmd = list(self.cmd)
    #    cmd.append("--config="+cfgfile)
    #    cmd.extend(timers.inputgen.wrap(lambda:self.getInputArg(testNumber)))
    #    if limit is not None:
    #      cmd.append("--max-sec=%f"%limit)
    #    cmd.extend(getMemoryLimitArgs())
            cfg = HalideConfigAccessor(ConfigFile(cfgfile))
            try:
                #results = timers.testing.wrap(lambda: runCommand(self.app, cfg, self.hl_cfg, limit))
                #for i,result in enumerate(results):
                #    if result is not None:
                #        v=result['average']
                #        if numpy.isnan(v) or numpy.isinf(v):
                #            warnings.warn(tunerwarnings.NanAccuracy())
                #            raise pbutil.TimingRunFailed(None)
                #        candidate.metrics[i][self.n].add(v)
                #return True
                T = runCommand(self.app, cfg, self.hl_cfg, limit, self.test_func, self.func_d, self.func, self.scope)
                #print 'succeeded'
                for timeval in T:
                    candidate.metrics[config.timing_metric_idx][self.n].add(timeval)
            except TimingRunTimeout:
                #assert limit is not None
                #warnings.warn(tunerwarnings.ProgramTimeout(candidate, self.n, limit))
                candidate.metrics[config.timing_metric_idx][self.n].addTimeout(limit)
                self.timeoutCount += 1
                return False
            except TimingRunFailed, e:
                self.crashCount += 1
                raise CrashException(testNumber, self.n, candidate, self.app)
        except:
            traceback.print_exc()
            raise
            
class HalidePopulation(sgatuner.Population):  
  def __init__(self, hl_cfg, *args):
    self.hl_cfg = hl_cfg
    sgatuner.Population.__init__(self, *args)
    
  def test(self, count):
    '''test each member of the pop count times'''
    if self.best is not None:
        print '------------------------------------------------'
        print 'Best schedule:'
        print self.hl_cfg.str(HalideConfigAccessor(ConfigFile(self.best.cfgfile())))
        print '------------------------------------------------'
    
    self.failed=set()
    tests = []
    for z in xrange(count):
      tests.extend(self.members)
    random.shuffle(tests)
    for m in tests:
      sgatuner.check_timeout()
      if m not in self.failed and m.numTests(self.inputSize())<config.max_trials:
        try:
          self.testers[-1].test(m)
        except candidatetester.CrashException, e:
          if m.numTotalTests()==0:
            warnings.warn(InitialProgramCrash(e))
          else:
            warnings.warn(ExistingProgramCrash(e))
          self.failed.add(m)
          self.members.remove(m)


def test():
    info = {}
    cfg = {}
    c = HalideHighLevelConfig(info, [('blur_x', ['x', 'y']),
                                     ('blur_y', ['x', 'y'])])
    seen = set()
    for i in range(20):
        for item in c.items:
            item.randomize(cfg, 1)
        s = c.str(cfg)
        print s
        print s in seen
        print
        seen.add(s)

def autotune(func, test, scope):
    """
    Autotunes func, using test(func) as a testing function that returns a time in seconds.
    """
    sys.argv = [sys.argv[0], 'dirname']
    info = {}
    cfg = HalideConfigAccessor()
    func_d = halide.all_funcs(func)
    hl = HalideHighLevelConfig(info, [(key, halide.func_varlist(value)) for (key, value) in func_d.items()])
    hl.randomize(cfg, 1)
    
    test_permutation()

    sgatuner.main(tester_lambda=lambda *a: HalideCandidateTester(hl, *a, test_func=test, func_d=func_d, func=func, scope=scope), pop_lambda=lambda *a: HalidePopulation(hl, *a),
                  hlconfig_lambda=lambda: hl, config_lambda=lambda: cfg)
    
def main():
    args = sys.argv[1:]
    if len(args) < 1:
        print 'halide_autotune.py dirname [options to sgatuner.py]'
        sys.exit(1)
    
    info = {}
    cfg = HalideConfigAccessor()
    hl = HalideHighLevelConfig(info, [('blur_x', ['x', 'y']),
                                      ('blur_y', ['x', 'y'])])
    hl.randomize(cfg, 1)
        
    
    test_permutation()

    sgatuner.main(tester_lambda=lambda *a: HalideCandidateTester(hl, *a), pop_lambda=lambda *a: HalidePopulation(hl, *a),
                  hlconfig_lambda=lambda: hl, config_lambda=lambda: cfg)
    
if __name__ == '__main__':
    main()
    #print system_timelimit('python test.py', 1.1)