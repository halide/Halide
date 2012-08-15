
import random
import operator

def permutation(L, i):
    L = list(L)
    ans = []
    for j in range(len(L)):
        k = i%len(L)
        ans.append(L[k])
        i = i / len(L)
        del L[k]
    return ans

def factorial(n):
    return reduce(operator.mul, range(1, n+1))

def pairwise_swaps(L, Lp):
    L = list(L)
    # Turn L into Lp using pairwise swaps (transposes)
    #print 'pairwise_swaps', L, Lp
    ans = []
    for i in range(len(L)):
        if L[i] != Lp[i]:
            j = Lp.index(L[i])
            #assert j < len(L), (L, Lp, i)
            ans.append((L[i], L[j]))
            (L[i], L[j]) = (L[j], L[i])
    return ans
    
def check_pairwise_swaps(L0):
    assert pairwise_swaps(L0, L0) == []
    for i in range(factorial(len(L0))):
        Lp = permutation(L0, i)
        L = list(L0)
        for (a, b) in pairwise_swaps(L0, Lp):
            ai = L.index(a)
            bi = L.index(b)
            assert ai != bi
            (L[ai], L[bi]) = (L[bi], L[ai])
    
def test_permutation():
    for n in range(1, 8):
        for j in range(10):
            L = [2*x+1 for x in range(n)]
            random.shuffle(L)
            #print L
            assert permutation(L, 0) == L, (permutation(L, 0), L)
            check_pairwise_swaps(L)
            nfac = factorial(n)
            setL = set(tuple(permutation(L,i)) for i in range(nfac))
            assert len(setL) == nfac, (n, L, nfac, len(setL), setL)
            
            
    print 'permutation: OK'

if __name__ == '__main__':
    test_permutation()
    