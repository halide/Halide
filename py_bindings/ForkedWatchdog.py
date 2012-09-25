#!/usr/bin/env python
# file: watchdog.py
# license: MIT License

# original source: http://www.dzone.com/snippets/simple-python-watchdog-timer

import signal,os,sys,time

def process_alive(pid):
    try:
        os.kill(pid, 0)
#        print 'alive', pid
        return True
    except:
#        print 'not alive', pid
        #traceback.print_exc()
        return False

def sleep_while_alive(T, pid):
    Tstep = 0.05
    T0 = time.time()
    while True:
        T1 = time.time()
        if T1-T0 > T:
            #print 'time above threshold', T1-T0, T
            return
        time.sleep(max(min(Tstep, T-(T1-T0)),0.0))
        #if os.waitpid(pid, os.WNOHANG) == (0,0):
        if not process_alive(pid):
            #print 'waitpid returned (0,0)'
            return

class Watchdog(Exception):
    def __init__(self, timeout=5, extraData=None):
        self.timeout = timeout
        self.parent_pid=os.getpid()
        self.extraData=extraData

    def __enter__(self):
        failed=True
        while failed:
            try:
                self.pid=os.fork()
                failed=False
            except OSError:
                pass

        if self.pid is 0: #not 0:           # In the child
            #time.sleep(self.timeout)
            #print 'sleep while alive', self.parent_pid
            sleep_while_alive(self.timeout, self.parent_pid)
            #print 'kill', self.parent_pid
            try:
                os.kill(self.parent_pid, signal.SIGKILL)
            except OSError:
                pass
            #print 'raise', self.pid
            raise self
        else:                           # In the child
            #print 'in parent, enter', self.pid
            pass
  
    def __exit__(self, type, value, traceback):
        #print 'exit', self.pid
        try:                        # In child or parent
            os.kill(self.pid, signal.SIGKILL)       # Kill child (if child), otherwise kill child (?)
        except OSError:
            pass

#    def __del__(self):
#        print 'del called'
        
    def __str__(self):
        return "Watchdog exception: %d second timeout." % self.timeout
        
    def getExtraData(self):
        return self.extraData


import unittest
import time

class TestWatchDog(unittest.TestCase):
    def test_no_alarm(self):
        with Watchdog(2):
            time.sleep(1)

    def test_alarm(self):
        def timed_long_func():
            with Watchdog(1):
                time.sleep(2)
        self.assertRaises(Watchdog, timed_long_func)

    def test_sigterm(self):
        def f():
            T0 = time.time()
            try:
                with Watchdog(5):
                    time.sleep(1.0)
                    os.kill(os.getpid(), signal.SIGTERM)
                return False
            except Watchdog:
                pass
            T1 = time.time()
            return (T1-T0) < 1.5
        self.assertTrue(f())

"""
def test():
    try:
        with Watchdog(5):
#            while 1:
#                time.sleep(1.0)
            time.sleep(1.0)
            print 'done sleep, killing', os.getpid()
            #sys.exit(0)
            os.kill(os.getpid(), signal.SIGTERM)
            print 'done kill'
    except Watchdog:
        print 'trapped'
    print 'done'
"""

if __name__=="__main__":
    unittest.main()
    #test()
    
