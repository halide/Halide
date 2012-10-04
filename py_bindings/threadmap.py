
"""
threadmap -- Threaded map(), uses all processors by default.

Connelly Barnes 2008-2011, public domain.
"""

import os, sys
import ctypes, ctypes.util
import time, thread, traceback, Queue, random, time

builtin_map = map

__version__ = '1.0.2'

# Change in 1.01: Includes map(..., dynamic=True) option for dynamic load balancing.

def nprocessors():
  try:
    try:
      try:
        try:
          import multiprocessing
          return multiprocessing.cpu_count()
        except:
          # Mac OS
          libc=ctypes.cdll.LoadLibrary(ctypes.util.find_library('libc'))
          v=ctypes.c_int(0)
          size=ctypes.c_size_t(ctypes.sizeof(v))
          libc.sysctlbyname('hw.ncpu', ctypes.c_voidp(ctypes.addressof(v)), ctypes.addressof(size), None, 0)
          return v.value
      except:
        # Cygwin (Windows) and Linuxes
        # Could try sysconf(_SC_NPROCESSORS_ONLN) (LSB) next.  Instead, count processors in cpuinfo.
        s = open('/proc/cpuinfo', 'r').read()
        return s.replace(' ', '').replace('\t', '').count('processor:')
    except:
      # Native (Windows)
      return int(os.environ.get('NUMBER_OF_PROCESSORS'))
  except:
    return 1

nproc = nprocessors()

def map(f, *a, **kw):
  """
  threadmap.map(..., n=nprocessors, dynamic=False), same as map(...).

  n must be a keyword arg; default n is number of physical processors.
  If dynamic (default False), uses (slow) dynamic load balancing otherwise uses static.
  """
  n = kw.get('n', nproc)
  if n == 1:
    return builtin_map(f, *a)
  dynamic = kw.get('dynamic', False)

  if len(a) == 1:
    L = a[0]
  else:
    L = zip(*a)
  try:
    len(L)
  except TypeError:
    L = list(L)
  n = min(n, len(L))

  ans = [None] * len(L)
  q = Queue.Queue()
  if not dynamic:
    def handle_static(start, end):
      try:
        if len(a) == 1:
          ans[start:end] = builtin_map(f, L[start:end])
        else:
          ans[start:end] = [f(*x) for x in L[start:end]]
      except Exception, e:
        q.put(sys.exc_info())
      else:
        q.put(None)

    [thread.start_new_thread(handle_static, (i*len(L)//n, (i+1)*len(L)//n)) for i in range(n)]
  else:
    qdyn = list(enumerate(L))

    def handle_dynamic():
      try:
        while 1:
          try:
            (idx, item) = qdyn.pop()
          except IndexError:
            break
          if len(a) == 1:
            ans[idx] = f(item)
          else:
            ans[idx] = f(*item)
      except Exception, e:
        q.put(sys.exc_info())
      else:
        q.put(None)

    [thread.start_new_thread(handle_dynamic, ()) for i in range(n)]

  for i in range(n):
    x = q.get()
    if x is not None:
      raise x[1], None, x[2]
  return ans

def bench():
  print 'Benchmark:\n'
  def timefunc(F):
    start = time.time()
    F()
    return time.time() - start
  def f1():
    return builtin_map(lambda x: pow(x,10**1000,10**9), range(10**3))
  def g1():
    return map(lambda x: pow(x,10**1000,10**9), range(10**3))
  def h1():
    return map(lambda x: pow(x,10**1000,10**9), range(10**3), dynamic=True)
  def f2():
    return builtin_map(lambda x: os.system(syscall), range(10**2))
  def g2():
    return map(lambda x: os.system(syscall), range(10**2))
  def h2():
    return map(lambda x: os.system(syscall), range(10**2), dynamic=True)
  #random.seed(1)
  sleeplist = [0.01, 0.01, 0.02, 0.03, 0.05, 0.08, 0.1, 0.2, 0.3, 0.4, 0.6, 0.8, 1.1, 1.5, 2.0] #[random.random()*5 for i in range(10)]
  def f3():
    return builtin_map(lambda x: time.sleep(x), sleeplist)
  def g3():
    return map(lambda x: time.sleep(x), sleeplist)
  def h3():
    return map(lambda x: time.sleep(x), sleeplist, dynamic=True)
  print 'Python operation, 10**3 items:'
  print 'map           (1 processor):          ', timefunc(f1), 's'
  print 'threadmap.map (%d processors, static): ' % nproc, timefunc(g1), 's'
  print 'threadmap.map (%d processors, dynamic):' % nproc, timefunc(h1), 's'
  print
  print 'Syscall, 10**2 items:'
  if sys.platform == 'win32':
    syscall = 'dir > NUL:'
  else:
    syscall = 'ls > /dev/null'
  for i in range(10):
    os.system(syscall)
  print 'map           (1 processor):          ', timefunc(f2), 's'
  print 'threadmap.map (%d processors, static): ' % nproc, timefunc(g2), 's'
  print 'threadmap.map (%d processors, dynamic):' % nproc, timefunc(h2), 's'
  print
  print 'Variable time sleep, 15 items:'
  print 'map           (1 processor):          ', timefunc(f3), 's'
  print 'threadmap.map (%d processors, static): ' % nproc, timefunc(g3), 's'
  print 'threadmap.map (%d processors, dynamic):' % nproc, timefunc(h3), 's'

def test():
  print 'Testing:'
  assert [x**2 for x in range(10**4)] == map(lambda x: x**2, range(10**4))
  assert [x**2 for x in range(10**4)] == map(lambda x: x**2, range(10**4), n=10)
  assert [x**2 for x in range(10**4)] == map(lambda x: x**2, range(10**4), n=1)
  assert [(x**2,) for x in range(10**3,10**4)] == map(lambda x: (x**2,), range(10**3,10**4))
  assert [(x**2,) for x in range(10**3,10**4)] == map(lambda x: (x**2,), range(10**3,10**4), n=10)
  assert [(x**2,) for x in range(10**3,10**4)] == map(lambda x: (x**2,), range(10**3,10**4), n=1)
  assert builtin_map(lambda x,y:x+2*y, range(100),range(0,200,2)) == map(lambda x,y:x+2*y, range(100),range(0,200,2))
  assert builtin_map(lambda x,y:x+2*y, range(100),range(0,200,2)) == map(lambda x,y:x+2*y, range(100),range(0,200,2), n=10)
  assert builtin_map(lambda x,y:x+2*y, range(100),range(0,200,2)) == map(lambda x,y:x+2*y, range(100),range(0,200,2), n=2)
  # Some Windows (Cygwin) boxes can't fork more than about 15 times, so only test to n=15
  for n in range(1, 15):
    assert [x**3 for x in range(200)] == map(lambda x: x**3, range(200), n=n)
  def f(n):
    if n == 1:
      raise KeyError
  def check_raises(func, exc):
    e = None
    try:
      func()
    except Exception, e:
      pass
    if not isinstance(e, exc):
      raise ValueError('function did not raise specified error')

  check_raises(lambda: map(f, [1, 0], n=2), KeyError)
  check_raises(lambda: map(f, [0, 1], n=2), KeyError)
  check_raises(lambda: map(f, [1, 0, 0], n=3), KeyError)
  check_raises(lambda: map(f, [0, 1, 0], n=3), KeyError)
  check_raises(lambda: map(f, [0, 0, 1], n=3), KeyError)
  print 'threadmap.map: OK'

if __name__ == '__main__':
  test()
  bench()
