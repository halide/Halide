// The traceback as an inductive Func over the step index, consuming the
// direction plane with data-dependent reads. State per pair is (i, j,
// state, op): the cell being visited, ksw_backtrack's current state, and
// the op emitted this step. Every lane walks the same fixed number of
// steps (query length + target length covers any path); a lane that has
// reached the origin idles, emitting op 3. Ops come out in backward
// order, as ksw_backtrack produces them before its final reversal.
//
// Only the self-reference tb(b, s-1) is constrained to be inductive; the
// read of dir at (jp, ip) is an ordinary data-dependent read of another
// Func, clamped so bounds inference asks for the whole plane.

#pragma once

#include "Halide.h"

inline Halide::Func align_traceback(Halide::Func dir, Halide::Expr J, Halide::Expr I,
                                    Halide::Var b, Halide::Var s) {
    using namespace Halide;
    Func tb(std::vector<Type>{Int(16), Int(16), UInt(8), UInt(8)}, 2, "tb");
    Expr ip = tb(b, s - 1)[0], jp = tb(b, s - 1)[1], stp = tb(b, s - 1)[2];
    Expr in_loop = ip >= 0 && jp >= 0;
    Expr tmp = dir(b, clamp(cast<int32_t>(jp), 0, J - 1), clamp(cast<int32_t>(ip), 0, I - 1));
    // ksw_backtrack: in state 0 take the byte's low bits; otherwise stay
    // only if the byte marks a continuation of this gap, else take them.
    Expr cont = ((tmp >> (stp + 2)) & 1) != 0;
    Expr st = select(stp == 0 || !cont, tmp & 7, stp);
    // ops: 0 = M (both move), 2 = D (target moves), 1 = I (query moves).
    Expr op_loop = select(st == 0, 0, select(st == 1, 2, 1));
    Expr op = select(in_loop, op_loop, select(ip >= 0, 2, select(jp >= 0, 1, 3)));
    Expr di = select(in_loop, select(st == 1, 1, select(st == 2, 0, 1)), select(ip >= 0, 1, 0));
    Expr dj = select(in_loop, select(st == 2, 1, select(st == 1, 0, 1)), select(ip >= 0, 0, select(jp >= 0, 1, 0)));
    Tuple base = {cast<int16_t>(I - 1), cast<int16_t>(J - 1), cast<uint8_t>(0), cast<uint8_t>(3)};
    Tuple step = {likely(cast<int16_t>(ip - cast<int16_t>(di))),
                  cast<int16_t>(jp - cast<int16_t>(dj)),
                  cast<uint8_t>(select(in_loop, st, 0)),
                  cast<uint8_t>(op)};
    tb(b, s) = select(s < 0, base, step);
    return tb;
}
