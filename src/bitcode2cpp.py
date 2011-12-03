#!/usr/bin/env python

import sys

target = sys.argv[1]

print 'unsigned char builtins_bitcode_' + target + '[] = {'
data = sys.stdin.read()
print ', '.join(str(ord(c)) for c in data), 
print ', 0};\n'
print "int builtins_bitcode_" + target + "_length = " + str(len(data)) + ";\n"


