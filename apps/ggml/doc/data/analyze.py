#!/usr/bin/env python3
"""G/H = t_GGML / t_ours per case, as the paper's Table 4 (>= 0.95: a match).

A case: (op, weight, act, K, N, M, threads). GGML's time: its fastest CPU
path (no BLAS, no GPU); the stratum is that path's kind: format-specific
(repack, AMX, or the standalone vec_dot op) or blocked-dot (ggml-cpu, which
loops vec_dot over the output). Ours: one stored layout per weight type,
held out: chosen (geomean G/H) on the case's other shapes; --per-case takes
the fastest layout per case (diagnostic). vec_dot: K in {4096, 14336}.
usage: analyze.py a.csv [b.csv ...] [--per-case] [--cases] [--by-m]
(several CSVs: the minimum time per row: the re-run rule)
"""
import csv, re, sys, math, statistics, collections

args = [a for a in sys.argv[1:] if not a.startswith('--')]
per_case, show, by_m = '--per-case' in sys.argv, '--cases' in sys.argv, '--by-m' in sys.argv
ACT = re.compile(r'(.*?)_(q8_0|q8_1|q8_K|f16|f32|bf16|qai8dx)(_|$)')
best = {}
for f in args:
    for r in csv.DictReader(open(f)):
        p = r['provider']
        if 'cuda' in p or 'metal' in p or p.endswith(':checked') or p == 'ggml-blas':
            continue
        if r['op'] == 'vec_dot' and r['K'] not in ('4096', '14336'):
            continue
        key = (r['op'], r['wtype'], r['atype'], r['K'], r['N'], r['M'], r['threads'], p, r['path'])
        best[key] = min(float(r['ns']), best.get(key, 1e30))

def layout(w, p):
    n = p[len('halide-'):]
    rest = n[len(w):] if n.startswith(w + '_') else '@' + n
    m = ACT.match(rest)
    return m.group(1) if m else rest

cases = collections.defaultdict(lambda: {'ggml': (1e30, '', ''), 'ours': {}})
for (op, w, a, K, N, M, T, p, path), t in best.items():
    c = cases[(op, w, a, K, N, M, T)]
    if p.startswith('halide-'):
        L = layout(w, p)
        c['ours'][L] = min(t, c['ours'].get(L, 1e30))
    elif p.startswith('ggml-') and t < c['ggml'][0]:
        c['ggml'] = (t, p, path)
cases = {k: c for k, c in cases.items() if c['ours'] and c['ggml'][1]}

def stratum(k, c):
    return 'blocked-dot' if k[0] == 'mul_mat' and c['ggml'][1] == 'ggml-cpu' else 'format-specific'

def chosen(k):
    if per_case:
        return min(cases[k]['ours'], key=cases[k]['ours'].get)
    op, w = k[0], k[1]
    score = collections.defaultdict(list)
    for k2, c2 in cases.items():
        if k2[:2] == (op, w) and k2[3:5] != k[3:5]:
            for L, t in c2['ours'].items():
                score[L].append(math.log(c2['ggml'][0] / t))
    n = max((len(v) for v in score.values()), default=0)
    full = {L: sum(v) / len(v) for L, v in score.items() if len(v) == n}
    if not full:  # one shape only: the fastest here
        return min(cases[k]['ours'], key=cases[k]['ours'].get)
    L = max(full, key=full.get)
    return L if L in cases[k]['ours'] else min(cases[k]['ours'], key=cases[k]['ours'].get)

res = collections.defaultdict(list)
for k, c in sorted(cases.items()):
    L = chosen(k)
    g = c['ggml'][0] / c['ours'][L]
    res[(k[0] + (f'@M{k[5]}' if by_m else ''), k[6], stratum(k, c))].append((g, k, L, c['ggml'][1] + ':' + c['ggml'][2]))

print('op,threads,stratum,cases,matched,pct,median')
for (op, T, s), v in sorted(res.items()):
    g = [x[0] for x in v]
    m = sum(x >= 0.95 for x in g)
    print(f"{op},{T},{s},{len(g)},{m},{100 * m / len(g):.1f},{statistics.median(g):.3f}")
if show:
    for key, v in sorted(res.items()):
        for g, k, L, theirs in sorted(v):
            print(f"{g:.3f} {key[2][:6]} {'/'.join(k[1:6])} T{k[6]} {k[0]} ours:{L or 'aos'} vs {theirs}")
