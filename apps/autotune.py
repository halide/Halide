import subprocess
import random
import sys

program = sys.argv[1]
# run once to get the time limit
output = subprocess.check_output([program])
last_line = output.split('\n')[-2]
time_limit = int(last_line)

def r():
    return random.randrange(10000)

def fitness(dna):
    try:
        output = subprocess.check_output([program, str(time_limit)] + [str(x) for x in dna])
    except subprocess.CalledProcessError:
        return float('inf')

    try:
        last_line = output.split('\n')[-2]
    except IndexError:
        return float('inf')

    try:
        t = float(last_line)/1000
    except ValueError:
        return float('inf')

    return t

def mutate(dna):
    if len(dna) == 0: return [r()]
    dna = dna[:]
    choice = random.randrange(10)
    if choice == 0: dna[random.randrange(len(dna))] = r()
    elif choice == 1: dna = [r() for x in dna]
    elif choice == 2: dna.pop(random.randrange(len(dna)))
    else: dna.append(r())
    return dna

population = [[] for i in range(16)]



while 1:
    # evaluate fitness
    fitnesses = [fitness(x) for x in population]

    print "Current population:"
    for (p, f) in zip(population, fitnesses):
        print ("%3.1f:\t" % f), ' '.join(str(x) for x in p)
        
    # take the best quarter of the population
    l = zip(fitnesses, population)
    l.sort()
    l = [x for (f, x) in l[:(len(l)/4)]]

    # add some mutations
    population = l + [mutate(x) for x in l] + [mutate(x) for x in l] + [mutate(x) for x in l] 
