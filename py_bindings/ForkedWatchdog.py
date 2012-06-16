#!/usr/bin/env python
# file: watchdog.py
# license: MIT License

# original source: http://www.dzone.com/snippets/simple-python-watchdog-timer

import signal,os,sys,time

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

        if self.pid is 0:
            time.sleep(self.timeout)
            try:
                os.kill(self.parent_pid, signal.SIGKILL)
            except OSError:
                pass
            raise self
        else:
            pass
  
    def __exit__(self, type, value, traceback):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass

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

if __name__=="__main__":
    unittest.main()
    
