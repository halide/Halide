// The traceback, consuming the direction plane with data-dependent
// reads. State per pair is (i, j, state, op): the cell being visited,
// ksw_backtrack's current state, and the op emitted this step. Every
// lane walks the same fixed number of steps (query length + target
// length covers any path); a lane that has reached the origin idles,
// emitting op 3. Ops come out in backward order, as ksw_backtrack
// produces them before its final reversal.
//
// Two expressions of the same walk. The inductive one is a Func over
// the step index with a one-back self-reference; only that reference is
// constrained, the read of dir at (jp, ip) being an ordinary
// data-dependent read of another Func, clamped so bounds inference asks
// for the whole plane. The RDom one is an update definition over the
// steps, for the fully-RDom ablation. The walk is a regime where the
// two should tie: the consumer wants every step, and the per-step state
// is six bytes.

#pragma once

#include "Halide.h"

namespace align_tb {

// The direction byte packed to four bits: ksw2's state in the low two
// (0, 1 or 2) and its two extension flags, 0x08 and 0x10, moved down to
// bits 2 and 3. Two cells share a byte, the even column in the low
// nibble.
inline Halide::Expr pack_dir(Halide::Expr byte) {
    using namespace Halide;
    return (byte & cast<uint8_t>(3)) | ((byte >> cast<uint8_t>(1)) & cast<uint8_t>(0xC));
}
inline Halide::Expr unpack_dir(Halide::Expr nibble) {
    using namespace Halide;
    return (nibble & cast<uint8_t>(3)) | ((nibble & cast<uint8_t>(0xC)) << cast<uint8_t>(1));
}

// One step of ksw_backtrack from the previous (i, j, state), reading the
// direction at that cell: a byte per cell, or with `packed` a nibble.
// Returns (i', j', state', op).
inline std::vector<Halide::Expr> step(Halide::Expr ip, Halide::Expr jp, Halide::Expr stp,
                                      Halide::Func dir, bool packed, Halide::Expr J, Halide::Expr I,
                                      Halide::Var b) {
    using namespace Halide;
    Expr in_loop = ip >= 0 && jp >= 0;
    Expr jc = clamp(cast<int32_t>(jp), 0, J - 1), ic = clamp(cast<int32_t>(ip), 0, I - 1);
    Expr shift = cast<uint8_t>(4 * (jc % 2));
    Expr tmp = packed ? unpack_dir((dir(b, jc / 2, ic) >> shift) & cast<uint8_t>(0xF)) : dir(b, jc, ic);
    // In state 0 take the byte's low bits; otherwise stay only if the
    // byte marks a continuation of this gap, else take them.
    Expr cont = ((tmp >> (stp + 2)) & 1) != 0;
    Expr st = select(stp == 0 || !cont, tmp & 7, stp);
    // ops: 0 = M (both move), 2 = D (target moves), 1 = I (query moves).
    Expr op_loop = select(st == 0, 0, select(st == 1, 2, 1));
    Expr op = select(in_loop, op_loop, select(ip >= 0, 2, select(jp >= 0, 1, 3)));
    Expr di = select(in_loop, select(st == 2, 0, 1), select(ip >= 0, 1, 0));
    Expr dj = select(in_loop, select(st == 1, 0, 1), select(ip >= 0, 0, select(jp >= 0, 1, 0)));
    return {cast<int16_t>(ip - cast<int16_t>(di)),
            cast<int16_t>(jp - cast<int16_t>(dj)),
            cast<uint8_t>(select(in_loop, st, 0)),
            cast<uint8_t>(op)};
}

inline Halide::Tuple start(Halide::Expr J, Halide::Expr I) {
    using namespace Halide;
    return {cast<int16_t>(I - 1), cast<int16_t>(J - 1), cast<uint8_t>(0), cast<uint8_t>(3)};
}

}  // namespace align_tb

// Inductive form: tb(b, s) is the state after step s; s < 0 is the start.
inline Halide::Func align_traceback(Halide::Func dir, bool packed, Halide::Expr J, Halide::Expr I,
                                    Halide::Var b, Halide::Var s) {
    using namespace Halide;
    Func tb(std::vector<Type>{Int(16), Int(16), UInt(8), UInt(8)}, 2, "tb");
    auto n = align_tb::step(tb(b, s - 1)[0], tb(b, s - 1)[1], tb(b, s - 1)[2], dir, packed, J, I, b);
    Tuple stepT = {likely(n[0]), n[1], n[2], n[3]};
    tb(b, s) = select(s < 0, align_tb::start(J, I), stepT);
    return tb;
}

// RDom form: storage index 0 is the start; index k is the state after
// step k-1, so the caller reads tb(b, s + 1) for step s. nsteps is the
// number of steps to walk (query length + target length).
struct TracebackRDom {
    Halide::Func tb;
    Halide::RVar rs;
};
inline TracebackRDom align_traceback_rdom(Halide::Func dir, bool packed, Halide::Expr J, Halide::Expr I,
                                          Halide::Expr nsteps, Halide::Var b, Halide::Var s) {
    using namespace Halide;
    Func tb(std::vector<Type>{Int(16), Int(16), UInt(8), UInt(8)}, 2, "tb");
    tb(b, s) = align_tb::start(J, I);
    RDom r(1, nsteps, "rs");
    auto n = align_tb::step(tb(b, r - 1)[0], tb(b, r - 1)[1], tb(b, r - 1)[2], dir, packed, J, I, b);
    tb(b, r) = Tuple(n[0], n[1], n[2], n[3]);
    return {tb, r.x};
}
