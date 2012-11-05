# Autotuner templates

import random
import autotune
    
def sample(varlist):
    "Sample template using given variable list."
    if len(varlist) < 2:
        raise autotune.MutateFailed
    x = varlist[0]
    y = varlist[1]
    L = ['.chunk(%(x)s).vectorize(%(x)s,%(n)d)',
         '.root().tile(%(x)s,%(y)s,_c0,_c1,%(n)d,%(n)d).vectorize(_c0,%(n)d).parallel(%(y)s)',
         '.root().parallel(%(y)s).vectorize(%(x)s,%(n)d)']
    if autotune.is_cuda():
        L.extend([
            '.root().cudaTile(%(x)s,%(y)s,%(n)d,%(n)d)'
        ]*3)
    
    r = random.randrange(len(L))
    n = random.choice([2,4,8])
    return L[r]%locals()

def main():
    autotune.set_cuda(True)
    for i in range(60):
        print sample(['x','y'])

if __name__ == '__main__':
    main()
    