# Autotuner templates

import random
import autotune
    
def sample(varlist):
    "Sample template using given variable list."
    if len(varlist) < 2:
        raise autotune.MutateFailed
    x = varlist[0]
    y = varlist[1]
    r = random.randrange(3)
    n = random.choice([2,4,8])
    if r == 0:
        return '.chunk(%(x)s).vectorize(%(x)s,%(n)d)'%locals()
    elif r == 1:
        return '.root().tile(%(x)s,%(y)s,_c0,_c1,%(n)d,%(n)d).vectorize(_c0,%(n)d).parallel(%(y)s)' % locals()
    elif r == 2:
        return '.root().parallel(%(y)s).vectorize(%(x)s,%(n)d)' % locals()

def main():
    for i in range(60):
        print sample(['x','y'])

if __name__ == '__main__':
    main()
    