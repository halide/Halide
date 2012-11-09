"""
Plot running time vs generation.
"""

import sys
import matplotlib
import os

def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if len(args) == 0:
        print 'autotune_plot tune_X/summary.txt [out.png]'
        sys.exit(1)    
    filename = args[0]
    out_filename = args[1] if len(args) > 1 else None
    tunedir = os.path.split(os.path.split(os.path.abspath(filename))[0])[1]
    # Parse summary file (a bit of a hack -- should really dump a plot xy points file)
    L = open(filename, 'rt').read().strip().split('\n')
    L = [x for x in L if not x.startswith('#')]
    gen = 1
    is_gen = False
    time_d = {}
    for i in range(len(L)):
        L_split = L[i].split()
        if len(L_split) == 0:
            continue
        val = L_split[0]
        try:
            val = float(val)
            if not is_gen:
                if len(L_split) >= 2 and L_split[1].startswith('ref'):
                    pass
                else:
                    time_d[gen] = min(time_d.get(gen, 1e100), val)
            if not gen in time_d:
                time_d[gen] = val
        except ValueError:
            if L[i].startswith('Generation'):
                is_gen = True
                gen = int(L[i].split()[1])
    x = []
    y = []
    for (xv, yv) in time_d.items():
        x.append(xv)
        y.append(yv*1000)
#    print x
#    print y
    if len(y) == 0:
        print 'No data, not plotting'
        return
        
    if out_filename is not None:
        matplotlib.use('Agg')     # headless

    import pylab

    pylab.plot(x, y)
    pylab.title('Best time vs Generation\n(%s)'%tunedir)
    pylab.ylabel('Best time [ms]')
    pylab.xlabel('Generation')
    pylab.ylim(0, max(y)*1.01)
    if out_filename is not None:
        pylab.savefig(out_filename, dpi=300)
    else:
        pylab.show()
    
if __name__ == '__main__':
    main()
