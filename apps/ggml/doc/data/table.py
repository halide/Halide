#!/usr/bin/env python3
"""Markdown table of GGML / Halide per weight format and M (> 1: Halide faster).

Each side's fastest CPU path per case (no GPU, BLAS or :checked providers);
GGML's activations f32 (its mul_mat quantizes them itself), ours any.
Several CSVs: the minimum time per provider (the re-run rule).
usage: table.py THREADS a.csv [b.csv ...]
"""
import csv, sys, math, statistics, collections

T, files = sys.argv[1], sys.argv[2:]
best = collections.defaultdict(lambda: {'g': 1e30, 'h': 1e30})
for f in files:
    for r in csv.DictReader(l for l in open(f) if not l.startswith('#')):
        p = r['provider']
        if r['threads'] != T or 'cuda' in p or 'metal' in p or p.endswith(':checked') or p == 'ggml-blas':
            continue
        side = 'h' if p.startswith('halide-') else 'g' if p.startswith('ggml-') else None
        if side is None or (side == 'g' and r['atype'] != 'f32' and r['wtype'] != r['atype']):
            continue
        k = (r['wtype'], int(r['M']))
        best[k][side] = min(best[k][side], float(r['ns']))
ws = sorted({w for w, _ in best})
ms = sorted({m for _, m in best})
print('| format | ' + ' | '.join(f'M={m}' for m in ms) + ' |')
print('|---|' + '---|' * len(ms))
col = collections.defaultdict(list)
for w in ws:
    cells = []
    for m in ms:
        v = best.get((w, m))
        if v and v['g'] < 1e30 and v['h'] < 1e30:
            g = v['g'] / v['h']
            col[m].append(g)
            cells.append(f'{g:.2f}')
        else:
            cells.append('-')
    print(f'| {w} | ' + ' | '.join(cells) + ' |')
print('| **>= 0.95** | ' + ' | '.join(f'{sum(g >= 0.95 for g in col[m])}/{len(col[m])}' for m in ms) + ' |')
print('| **median** | ' + ' | '.join(f'{statistics.median(col[m]):.2f}' for m in ms) + ' |')
print('| **geomean** | ' + ' | '.join(f'{math.exp(sum(map(math.log, col[m])) / len(col[m])):.2f}' for m in ms) + ' |')
