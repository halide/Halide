
from autotune import *

# --------------------------------------------------------------------------------------------------------------
# Unit Tests
# --------------------------------------------------------------------------------------------------------------

def test_crossover(verbose=False):
    (f, g, locals_d) = test_funcs()
    constraints = Constraints()
    
    for j in range(8):
        random.seed(j)

#        for i in range(1000):
#            a = nontrivial_schedule(g)
#            a.apply()
#        print 'random_schedule:  OK'
        
        a = nontrivial_schedule(g)
        aL = str(a).split('\n')
        while True:
            b = nontrivial_schedule(g)
            bL = str(b).split('\n')
            all_different = True
            assert len(aL) == len(bL), (a, b, aL, bL)
            for i in range(len(aL)):
                if aL[i] == bL[i]:
                    all_different = False
                    break
            if all_different:
                break
        #print 'test_crossover'
        #print '----- Schedule A -----'
        #print a
        #print 
        #print '----- Schedule B -----'
        #print b
        #T0 = time.time()
        #print 'a'
        for i in range(80):
            c = crossover(a, b, constraints)
            c.apply(constraints)
            cL = str(c).split('\n')
            assert aL != bL and aL != cL and bL != cL
            if verbose:
                print '---- Crossover %d,%d ----'%(j,i)
                print c
        #T1 = time.time()
        
        p = AutotuneParams()
        #print 'b'
        for i in range(80):
            if verbose:
                print '---- Mutate after crossover %d,%d ----'%(j,i)
                print 'a', repr(str(a)), a.new_vars()
                print 'b', repr(str(b)), b.new_vars()
            c = crossover(a, b, constraints)
            if verbose:
                print 'c', repr(str(c)), c.new_vars()
            c.apply(constraints)
            c = mutate(c, p, constraints, None)
            if verbose:
                print 'cmutate', repr(str(c)), c.new_vars()
            c.apply(constraints)
    #        cL = str(c).split('\n')
    #        assert aL != bL and aL != cL and bL != cL

        #T2 = time.time()
        #print T1-T0, T2-T1
        
        def test_generation(L, prevL):
            assert len(L) == p.population_size
            for x in L:
                #print '-'*40
                #print x.title()
                #print x
                x.apply(constraints)
            current_set = set(str(x) for x in L)
            prev_set = set(str(x) for x in prevL)
            assert len(current_set) > 2
            if len(prev_set):
                assert len(current_set & prev_set) >= 1
        
        #random.seed(2)
        #print 'c'
        prev_gen = []
        for gen in range(2):
            L = next_generation(prev_gen, p, g, constraints, 0, [{'time': 0.1} for i in range(len(prev_gen))])
            if j == 0 and verbose:
                for i in range(len(L)):
                    print 'gen=%d, i=%d'%(gen,i)
                    print L[i]
                    print '-'*20
            test_generation(L, prev_gen)
            prev_gen = L
    
    print 'autotune.crossover:           OK'
    print 'autotune.mutate:              OK'
    print 'autotune.next_generation:     OK'

def test_funcs(cache=[]):
    if len(cache):
        return cache[0]
    f = halide.Func('f')
    x = halide.Var('x')
    y = halide.Var('y')
    c = halide.Var('c')
    g = halide.Func('g')
    v = halide.Var('v')
    input = halide.UniformImage(halide.UInt(16), 3)
    int_t = halide.Int(32)
    f[x,y,c] = input[halide.clamp(x,halide.cast(int_t,0),halide.cast(int_t,input.width()-1)),
                     halide.clamp(y,halide.cast(int_t,0),halide.cast(int_t,input.height()-1)),
                     halide.clamp(c,halide.cast(int_t,0),halide.cast(int_t,2))]
    #g[v] = f[v,v]
    g[x,y,c] = f[x,y,c]+1
    cache.append((f, g, locals()))
    return (f, g, locals())

def nontrivial_schedule(g):
    while True:
        r = random_schedule(g, 0, DEFAULT_MAX_DEPTH)
        s = str(r)
        if len(s) > 30:
            contains_num = False
            for i in range(65):
                if str(i) in s:
                    contains_num = True
                    break
            if contains_num:
                return r

def test_sample_prob():
    d = {'a': 0.3, 'b': 0.5, 'c': 0.2, 'd': 0.0}
    count = 10000
    dc = {'a': 0, 'b': 0, 'c': 0, 'd': 0}
    for i in range(count):
        key = sample_prob(d)
        dc[key] += 1
    eps = 0.05
    for key in d.keys():
        assert abs(dc[key]*1.0/count-d[key])<eps, (key, d[key], dc[key]*1.0/count)
    assert dc['d'] == 0
    print 'autotune.sample_prob:         OK'

def test_schedules(verbose=False, test_random=False):
    #random_module.seed(int(sys.argv[1]) if len(sys.argv)>1 else 0)
    constraints = Constraints()
    halide.exit_on_signal()
    (f, g, locals_d) = test_funcs()
    assert sorted(halide.all_vars(g).keys()) == sorted(['x', 'y', 'c']) #, 'v'])

    if verbose:
        print halide.func_varlist(f)
        print 'caller_vars(f) =', caller_vars(g, f)
        print 'caller_vars(g) =', caller_vars(g, g)
    
#    validL = list(valid_schedules(g, f, 4))
#    validL = [repr(_x) for _x in validL]
#    
#    for L in sorted(validL):
#        print repr(L)
    partial_schedule = Schedule.fromstring(g, '')
    T0 = time.time()
    if not test_random:
        random = True #False
        nvalid_determ = 0
        for L in schedules_func(g, f, 0, 3, partial_schedule=partial_schedule):
            nvalid_determ += 1
            if verbose:
                print L

    nvalid_random = 0
    for i in range(100):
        for L in schedules_func(g, f, 0, DEFAULT_MAX_DEPTH, random=True, partial_schedule=partial_schedule): #sorted([repr(_x) for _x in valid_schedules(g, f, 3)]):
            if verbose and 0:
                print L#repr(L)
            nvalid_random += 1

    s = []
    for i in range(1000):
        d = random_schedule(g, 1, DEFAULT_MAX_DEPTH)
        si = str(d)
        si2 = str(Schedule.fromstring(g, si))
        if si != si2:
            print '-'*40
            print 'd'
            print d
            print '-'*40
            print 'si'
            print si
            print '-'*40
            print 'si2'
            print si2
            raise ValueError
        
        s.append(si)
        if verbose:
            print 'Schedule:'
            print '-'*20
            print si
            print '-'*20
            print
        sys.stdout.flush()
        d.apply(constraints)
        #if test_random:
        #    evaluate = d.test((36, 36, 3), locals_d['input'], Constraints())
        #    print 'evaluate'
        #    evaluate()
        #    if test_random:
        #        print 'Success'
        #        sys.exit()
    T1 = time.time()
    
    s = '\n'.join(s)
    assert 'f.chunk(_c0)' in s
    assert 'f.root().vectorize' in s
    assert 'f.root().unroll' in s
    assert 'f.root().split' in s
    assert 'f.root().tile' in s
    assert 'f.root().parallel' in s
    assert 'f.root().reorder' in s

    assert nvalid_random == 100
    if verbose:
        print 'generated in %.3f secs' % (T1-T0)

    print 'autotune.random_schedule:     OK'
    
    r = nontrivial_schedule(g)
    constantL = [str(r)]
#    print 'Randomizing the constants in a chosen schedule:'
    for i in range(100):
        #print r.randomized_const()
        #print
        constantL.append(str(r.randomized_const()))
    assert len(set(constantL)) > 1
    print 'autotune.randomized_const:    OK'
            
def test():
    random.seed(0)
    test_sample_prob()
#    test_schedules(True)
    test_schedules(test_random=True)
    test_crossover()
