"""
Plot running time vs generation.
"""

import sys
import matplotlib
import os

def main():
    args = sys.argv[1:]
    if len(args) == 0:
        print 'autotune_plot tune_X/summary.txt [out.png]'
    
    filename = args[0]
    out_filename = args[1] if len(args) > 1 else None
    tunedir = os.path.split(os.path.split(os.path.abspath(filename))[0])[1]
    # Parse summary file (a bit of a hack -- should really dump a plot xy points file)
    L = open(filename, 'rt').read().strip().split('\n')
    gen = 1
    time_d = {}
    for i in range(len(L)):
        if len(L[i].split()) == 0:
            continue
        val = L[i].split()[0]
        try:
            val = float(val)
            if not gen in time_d:
                time_d[gen] = val
        except ValueError:
            if L[i].startswith('Generation'):
                gen = int(L[i].split()[1])
    x = []
    y = []
    for (xv, yv) in time_d.items():
        x.append(xv)
        y.append(yv)

    if out_filename is not None:
        matplotlib.use('Agg')     # headless

    import pylab

    pylab.plot(x, y)
    pylab.title('Best time vs Generation\n(%s)'%tunedir)
    pylab.ylabel('Best time [sec]')
    pylab.xlabel('Generation')
    if out_filename is not None:
        pylab.savefig(out_filename, dpi=300)
    else:
        pylab.show()
    
if __name__ == '__main__':
    main()
