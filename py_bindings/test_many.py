import os
import subprocess

cmd = 'python autotune.py autotune_child_process examples.local_laplacian.filter_func "" lena_crop.png 5 0'
nproc = 2
L = []
for i in range(nproc):
  L.append(subprocess.Popen(cmd, shell=True))


