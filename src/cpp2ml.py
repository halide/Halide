#!/usr/bin/env python

import sys

def filt(s):
    ignore = ['#include "buffer.h"']
    for i in ignore:
        s = s.replace(i, "")
    return s

def escape(s):
    special = ['"']
    for c in special:
        s = s.replace(c, '\\'+c)
    return s

with open(sys.argv[1]) as f:
  print 'let stdlib = "\n'
  data = f.read()
  print escape(repr(filt(data))[1:-1])
  print '\n"'
