
import halide
import autotune
import random

def get_bounds(root_func, scope):
    """
    Returns map of func name => list of bounds for each dim, with -1 indicating unbounded. Does not do bound inference.
    """
    bounds = {}
    varlist = {}
    for (name, f) in halide.all_funcs(root_func).items():
        varlist[name] = halide.func_varlist(f)
        bounds[name] = [-1] * len(varlist[name])
    if 'tune_constraints' in scope:
        constraints = autotune.Schedule.fromstring(root_func, scope['tune_constraints'])
    else:
        return bounds
    for (name, L) in constraints.d.items():
        for x in L:
            if isinstance(x, autotune.FragmentBound):
                try:
                    i = varlist[name].index(x.var)
                except IndexError:
                    raise ValueError('could not find var %s to bound in func %s (varlist is %r)' % (x.var, name, varlist[name]))
                bounds[name][i] = x.size
    
    return bounds

def get_xy(f, bounds):
    """
    Given func object and bounds returned by get_bounds(), return vars suitable for interpretation as (x, y).
    
    If this is not possible then just returns the var list for func.
    """
    varlist = halide.func_varlist(f)
    bounds = bounds[f.name()]
    assert len(varlist) == len(bounds), (len(varlist), len(bounds))
    allowed = [True]*len(varlist)
    for i in range(len(bounds)):
        if bounds[i] < 10 and bounds[i] >= 0:
            allowed[i] = False

    ans = []
    for i in range(len(bounds)-1):
        if allowed[i] and allowed[i+1]:
            ans.append((varlist[i], varlist[i+1]))
    if len(ans) > 0:
        return random.choice(ans)
    return varlist
    
def test():
#    from examples.interpolate import filter_func
    from examples.camera_pipe import filter_func
    (input, output, evaluate, scope) = filter_func()
    bounds = get_bounds(output, scope)
    print bounds
    print get_xy(output, bounds)
#    print get_xy(halide.all_funcs(output)['ux6'], bounds)

if __name__ == '__main__':
    test()
