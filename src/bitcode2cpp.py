#!/usr/bin/env python

# Stolen from ISPC

import sys
import string
import re
import subprocess
import platform
import os

length=0

src=str(sys.argv[1])

target = re.sub(".*builtins-", "", src)
target = re.sub("\.ll$", "", target)
target = re.sub("\.c$", "", target)
target = re.sub("-", "_", target)

llvm_as="llvm-as"
if platform.system() == 'Windows' or string.find(platform.system(), "CYGWIN_NT") != -1:
    llvm_as = os.getenv("LLVM_INSTALL_DIR").replace("\\", "/") + "/bin/" + llvm_as

try:
    as_out=subprocess.Popen([llvm_as, "-", "-o", "-"], stdout=subprocess.PIPE)
except IOError:
    print >> sys.stderr, "Couldn't open " + src
    sys.exit(1)

print "unsigned char builtins_bitcode_" + target + "[] = {"
for line in as_out.stdout.readlines():
    length = length + len(line)
    for c in line:
        print ord(c)
        print ", "
print " 0 };\n\n"
print "int builtins_bitcode_" + target + "_length = " + str(length) + ";\n"

as_out.wait()

sys.exit(as_out.returncode)
