# Autotuner templates

import random
import autotune

CHUNK_X_ALWAYS = False

def sample(varlist, schedule, name):
    "Sample template using given variable list."
    if len(varlist) < 2:
        raise autotune.MutateFailed
#    x = varlist[0]
#    y = varlist[1]
    x = varlist[1]
    y = varlist[2]
    
    L = ['.chunk(%(chunk_var)s).vectorize(%(x)s,%(n)d)',
         '.root().tile(%(x)s,%(y)s,_c0,_c1,%(n)d,%(n)d).vectorize(_c0,%(n)d).parallel(%(y)s)',
         '.root().parallel(%(y)s).vectorize(%(x)s,%(n)d)']
    if autotune.is_cuda():
        L.extend([
            '.root().cudaTile(%(x)s,%(y)s,%(n)d,%(n)d)'
        ]*3)
    
    r = random.randrange(len(L))
    if r == 0:
        if CHUNK_X_ALWAYS:
            chunk_var = x
        else:
            cvars = list(autotune.chunk_vars(schedule, schedule.d[name].func))
            if '_c0' in cvars:
                cvars = cvars[:cvars.index('_c0'):] + cvars[cvars.index('_c0')+1:]
            if len(cvars) == 0:
                raise autotune.MutateFailed
            else:
                chunk_var = random.choice(cvars)
    n = random.choice([2,4,8])
    return L[r]%locals()

def main():
    autotune.set_cuda(True)
    for i in range(60):
        print sample(['x','y'])

if __name__ == '__main__':
    main()
    