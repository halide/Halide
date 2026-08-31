// Halide tutorial lesson 25: Aligned splits

// This lesson demonstrates Func::split's 'align' parameter: a way to anchor
// a split's tile boundaries to a value that's only known at runtime (e.g. a
// Param), instead of to the Var's own min. It shows what that buys you --
// primarily, letting mux() calls whose selector depends on that same
// runtime value collapse to the single case they select, instead of
// surviving as mux() calls whose case can only be picked at run time --
// and where the trick currently runs out of steam: crossing a compute_at
// boundary.

// On linux, you can compile and run it like so:
// g++ lesson_25*.cpp -g -I <path/to/include> -L <path/to/lib> -lHalide -lpthread -ldl -o lesson_25 -std=c++17
// LD_LIBRARY_PATH=<path/to/lib> ./lesson_25

// On macOS:
// g++ lesson_25*.cpp -g -I <path/to/include> -L <path/to/lib> -lHalide -o lesson_25 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/lib> ./lesson_25

#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// A small IRVisitor used throughout this lesson to check what a schedule
// actually did to the compiled code: how many for loops remain, how wide
// they are, and how many mux() and % operations the simplifier managed to
// fold away. Ordinary Halide code never needs to do this -- it's only here
// so this lesson can show what each schedule accomplishes, rather than
// just assert it.
class Counter : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->is_intrinsic(Call::IntrinsicOp::mux)) {
            mux_count++;
        }
    }
    void visit(const Mod *op) override {
        IRVisitor::visit(op);
        mod_count++;
    }
    void visit(const For *op) override {
        extents.push_back(simplify(op->extent()));
        IRVisitor::visit(op);
        for_count++;
    }

public:
    int for_count = 0, mux_count = 0, mod_count = 0;
    std::vector<Expr> extents;
};

Counter count(const Module &m) {
    Counter c;
    for (const LoweredFunc &lf : m.functions()) {
        lf.body.accept(&c);
    }
    return c;
}

// Finds the "produce <name> { ... }" node for a given Func in a compiled
// Module, so this lesson can print the actual generated code for just that
// Func instead of the whole pipeline. This is the most direct way to see
// what a schedule bought you: read the loop nest and check for yourself
// whether a mux() call turned into a straight-line value or is still there
// as a mux() call with a non-constant selector. Since only that one
// subtree is printed, in a compute_at example you may see it reference a
// variable (e.g. a "let t123 = ...") that's bound just outside the
// printed excerpt, in the surrounding loop nest -- that's expected; the
// thing to focus on is the shape of the code inside, not resolving every
// hoisted temporary.
class FindProducer : public IRVisitor {
    using IRVisitor::visit;

    void visit(const ProducerConsumer *p) override {
        // When a Func's own name collides with the compiled Module's
        // exported function name, Halide disambiguates the internal one by
        // appending "$N" (Halide's general name-uniquification suffix), so
        // match that too rather than only an exact name.
        if (p->is_producer && (p->name == name || starts_with(p->name, name + "$"))) {
            producer = p;
        }
        IRVisitor::visit(p);
    }

public:
    FindProducer(std::string name) : name(std::move(name)) {}

    std::string name;
    Stmt producer;
};

Stmt find_producer(const Module &m, const std::string &name) {
    FindProducer f{name};
    for (const LoweredFunc &lf : m.functions()) {
        lf.body.accept(&f);
        if (f.producer.defined()) {
            return f.producer;
        }
    }
    return Stmt{};
}

}  // namespace

int main(int argc, char **argv) {

    // Part 1: what does "aligned" actually change?
    //
    // An ordinary split(x, xo, xi, factor) tiles a Var starting at its own
    // loop_min: tile boundaries fall at old_min, old_min + factor,
    // old_min + 2*factor, and so on. split(x, xo, xi, factor, align)
    // anchors the tiling to 'align' instead, wherever that falls relative
    // to old_min. 'align' need not be known at compile time -- that's the
    // whole point of the feature -- but to see *why* the anchor point
    // matters at all, this first example uses a compile-time-constant
    // align of 1, and looks at how it interacts with loop partitioning.
    {
        // The pipeline below is 8 elements: a boundary value at x == 0, a
        // boundary value at x == 7, and a period-2 pattern (x % 2) for the
        // six elements in between. likely() marks the interior case as the
        // expected one, which is what invites the loop partitioner to try
        // to peel the boundary cases into their own prologue/epilogue
        // rather than testing for them on every iteration.
        //
        // First, split by 2 with no alignment at all, and unroll the
        // 2-wide inner loop so that x % 2 has a chance to become a
        // compile-time constant in each unrolled copy.
        Var x("x"), xo("xo"), xi("xi");
        Func plain("plain");
        plain(x) = select(x <= 0, 100,
                           x < 7, likely(x % 2),
                           200);
        plain.bound(x, 0, 8);
        plain.split(x, xo, xi, 2, TailStrategy::GuardWithIf)
            .always_partition(xo)
            .unroll(xi);
        auto mod_plain = plain.compile_to_module({}, "plain");
        std::cout << "Without alignment, tiled from x == 0:\n"
                   << find_producer(mod_plain, "plain") << "\n";
        Counter c_plain = count(mod_plain);

        // Now the same thing, but anchored to x == 1 -- where the
        // interior's period-2 pattern actually begins -- instead of to
        // the Func's own min of 0. (Func's assignment operator is a
        // reference, not a clone, so this needs its own Func and Vars,
        // not a copy of 'plain'.)
        Var x2("x"), xo2("xo"), xi2("xi");
        Func aligned("aligned");
        aligned(x2) = select(x2 <= 0, 100,
                              x2 < 7, likely(x2 % 2),
                              200);
        aligned.bound(x2, 0, 8);
        aligned.split(x2, xo2, xi2, 2, /* align */ 1, TailStrategy::GuardWithIf)
            .always_partition(xo2)
            .unroll(xi2);
        auto mod_aligned = aligned.compile_to_module({}, "aligned");
        std::cout << "Anchored at x == 1:\n"
                   << find_producer(mod_aligned, "aligned") << "\n";
        Counter c_aligned = count(mod_aligned);

        // Either way, x % 2 folds away completely: every unrolled
        // instance of xi turns (xo*2 + align + xi) % 2 into a compile-time
        // constant, regardless of what 'align' is. (Try it: the multiple
        // of 2 drops out of the modulo either way.) So this isn't about
        // the modulo -- it's about how much of the *boundary* the
        // partitioner can peel into single, non-looping iterations.
        if (c_plain.mod_count != 0 || c_aligned.mod_count != 0) {
            printf("Expected the %% to fold away in both cases\n");
            return 1;
        }

        // Without alignment, tiles start at x == 0, so the single-element
        // boundary case at x == 0 *is* a whole tile, and the one at x == 7
        // spills into the tile that starts at x == 6. The partitioner has
        // to peel a full 2-element tile off of each end to isolate the
        // boundary, leaving a steady-state loop that only covers x in
        // [2, 5]: two unrolled-by-2 iterations.
        //
        // Anchored at x == 1, tiles start at 1, 3, 5: exactly where the
        // interior's period-2 pattern repeats. Now the single-element
        // boundary cases at x == 0 and x == 7 are each a partial tile of
        // their own, so the partitioner peels exactly one element off each
        // end, leaving a steady-state loop over x in [1, 6]: three
        // unrolled-by-2 iterations, one more than the unaligned version.
        if (c_plain.for_count != 1 || !is_const(c_plain.extents[0], 2)) {
            printf("Expected one steady-state loop of extent 2 without alignment\n");
            return 1;
        }
        if (c_aligned.for_count != 1 || !is_const(c_aligned.extents[0], 3)) {
            printf("Expected one steady-state loop of extent 3 with alignment\n");
            return 1;
        }

        // Both schedules compute the same answer either way -- alignment
        // only changes how the work is split into loops, never the
        // result.
        Buffer<int> out = aligned.realize({8});
        for (int i = 0; i < 8; i++) {
            int expected = (i <= 0) ? 100 : (i < 7) ? i % 2 :
                                                       200;
            if (out(i) != expected) {
                printf("out(%d) = %d instead of %d\n", i, out(i), expected);
                return 1;
            }
        }
    }

    // Part 2: anchoring a split to a runtime Param
    //
    // Lesson 13 pointed out that mux(id, {...}) is sugar for a select
    // chain, and that the select can be compiled away by bounding and
    // unrolling the Var it's indexed by -- but only if 'id' becomes a
    // compile-time constant in each unrolled copy. That's easy when 'id'
    // is directly the unrolled Var. It's harder when 'id' depends on that
    // Var only after subtracting off a value that's not known until
    // runtime, as below.
    //
    // (mux() itself is what you'll see printed below when it doesn't
    // fold: the mux() intrinsic survives untouched through every lowering
    // pass that runs before compile_to_module returns, which is the level
    // this lesson inspects. It isn't rewritten into an actual select()
    // chain until final code generation -- see lower_mux() in
    // CodeGen_Internal.cpp, called from within CodeGen_LLVM.cpp/
    // CodeGen_C.cpp -- well past what's printed here.)
    {
        // Each element's treatment depends on (x - offset) % 4: which of
        // 4 cases applies to a given x shifts by 'offset', a Param whose
        // value isn't known until the pipeline actually runs.
        Var x("x"), xo("xo"), xi("xi");
        Func f("f");
        Param<int> offset("offset");
        offset.set_range(0, 3);
        f(x) = mux((x - offset) % 4,
                   {x, x * x, 2 * x, -x * (x + 1)});
        f.output_buffer().dim(0).set_min(0);

        // An ordinary split(x, xo, xi, 4) makes xi range over [0, 3),
        // counting up from x's own min -- which has nothing to do with
        // 'offset'. (x - offset) % 4 would still depend on 'offset' after
        // substituting in xi, so it can never become a compile-time
        // constant, and every mux() call has to stay a mux() call with a
        // non-constant selector, no matter what you unroll. See for
        // yourself:
        Func f0("f0");
        f0(x) = mux((x - offset) % 4,
                    {x, x * x, 2 * x, -x * (x + 1)});
        f0.output_buffer().dim(0).set_min(0);
        f0.split(x, xo, xi, 4, TailStrategy::GuardWithIf)
            .unroll(xi);
        auto mod0 = f0.compile_to_module({offset}, "f0");
        std::cout << "Unaligned split, unrolled: the mux() is still there, selector not constant\n"
                   << find_producer(mod0, "f0") << "\n";

        // split(x, xo, xi, 4, offset) anchors tile boundaries to 'offset'
        // instead: x becomes xo*4 + offset + xi, with xi still ranging
        // over the plain [0, 4). Substitute that into the mux's selector
        // and 'offset' cancels algebraically -- (xo*4 + offset + xi -
        // offset) % 4 simplifies to xi % 4 -- with no need for xi to be a
        // literal yet. Halide's tail strategy here is GuardWithIf: since
        // the split factor doesn't necessarily divide the extent, the
        // last partial tile is handled by a separate, guarded copy of the
        // loop body rather than by recomputing or overrunning storage.
        f.split(x, xo, xi, 4, offset, TailStrategy::GuardWithIf)
            // xi % 4 is now a compile-time-provable value in [0, 4), but
            // it's still a runtime loop variable, not a literal -- so the
            // mux's selector is *known to be one of 4 cases*, but not
            // *which* one, and it would still be a mux() call with that
            // non-constant selector. Unrolling turns each iteration of xi
            // into its own copy of the loop body with xi replaced by a
            // literal 0, 1, 2, or 3, which is what finally lets each
            // mux() collapse to the single case it selects.
            .unroll(xi);

        Module m = f.compile_to_module({offset});
        std::cout << "Aligned split, GuardWithIf, unrolled: every mux() is gone\n"
                   << find_producer(m, "f") << "\n";
        Counter c = count(m);
        if (c.mux_count != 0) {
            printf("Expected every mux() to fold away, found %d left\n", c.mux_count);
            return 1;
        }

        // The pipeline still computes the same thing regardless of the
        // runtime value of 'offset' -- aligning the split doesn't change
        // the algorithm, only how completely the compiler can simplify
        // the code that implements it.
        for (int o = 0; o < 4; o++) {
            offset.set(o);
            Buffer<int> im = f.realize({32});
            for (int x = 0; x < 32; x++) {
                int selector = (4 + x - o) % 4;
                int expected = (selector == 0) ? x :
                               (selector == 1) ? x * x :
                               (selector == 2) ? 2 * x :
                                                  -x * (x + 1);
                if (im(x) != expected) {
                    printf("im(%d) = %d instead of %d (offset: %d)\n", x, im(x), expected, o);
                    return 1;
                }
            }
        }

        // The tail strategy matters here. GuardWithIf isolates the
        // partial last tile behind an explicit boundary check, so the
        // steady-state loop body only ever sees whole, uniformly-shifted
        // tiles, and every mux() in it folds. ShiftInwards instead slides
        // the last tile backwards to keep it in bounds, folding it into
        // the same loop as everything else -- which keeps the loop count
        // down to one, but that shifted tile is no longer offset from
        // 'offset' by a compile-time-constant amount, so the muxes serving
        // it can't be resolved at compile time and survive as mux() calls
        // with a non-constant selector.
        Func f2("f2");
        f2(x) = mux((x - offset) % 4,
                    {x, x * x, 2 * x, -x * (x + 1)});
        f2.output_buffer().dim(0).set_min(0);
        f2.split(x, xo, xi, 4, offset, TailStrategy::ShiftInwards)
            .unroll(xi);
        Module m2 = f2.compile_to_module({offset});
        std::cout << "Aligned split, ShiftInwards, unrolled: some mux() calls remain\n"
                   << find_producer(m2, "f2") << "\n";
        Counter c2 = count(m2);
        if (c2.mux_count == 0) {
            printf("Expected ShiftInwards to leave some muxes behind\n");
            return 1;
        }
        printf("GuardWithIf: %d for loop(s), %d mux() left. ShiftInwards: %d for loop(s), %d mux() left.\n",
               c.for_count, c.mux_count, c2.for_count, c2.mux_count);
    }

    // Part 3: two dimensions at once
    //
    // The same idea applies independently in each dimension: split both x
    // and y, anchored to their own runtime offsets.
    {
        Var x("x"), xo("xo"), xi("xi");
        Var y("y"), yo("yo"), yi("yi");
        Func f("f");
        Param<int> offset_x("offset_x"), offset_y("offset_y");
        offset_x.set_range(0, 1);
        offset_y.set_range(0, 1);
        Expr selector = 2 * ((y - offset_y) % 2) + (x - offset_x) % 2;
        f(x, y) = mux(selector, {x * x, x * y, y * y, x + y});
        f.output_buffer().dim(0).set_min(0);
        f.output_buffer().dim(1).set_min(0);

        f.split(x, xo, xi, 2, offset_x, TailStrategy::GuardWithIf)
            .split(y, yo, yi, 2, offset_y, TailStrategy::GuardWithIf)
            // Both tail strategies here already guard their own partial
            // tile explicitly, so there's nothing for the loop partitioner
            // to usefully split further. Turning it off keeps the compiled
            // code -- and the for-loop count checked below -- simple and
            // predictable.
            .never_partition_all()
            // xi and yi need to be innermost, and unrolled, for the same
            // reason as Part 2: that's what turns them into the literals
            // that let the mux's selector become a compile-time constant.
            .reorder(xi, yi, xo, yo)
            .unroll(xi)
            .unroll(yi);

        Module m = f.compile_to_module({offset_x, offset_y});
        std::cout << "2D aligned split, both dims unrolled: no mux() left\n"
                   << find_producer(m, "f") << "\n";
        Counter c = count(m);
        if (c.mux_count != 0) {
            printf("Expected every mux() to fold away, found %d left\n", c.mux_count);
            return 1;
        }
        printf("2D case: %d for loop(s), %d mux() left.\n", c.for_count, c.mux_count);
    }

    // Part 4: compute_at reintroduces the problem
    //
    // The mux-folding trick above relies on the unrolled Var being
    // directly related to the aligned split's own outer Var by a
    // compile-time-constant offset. That relationship doesn't survive
    // crossing a compute_at boundary for free: a Func computed at some
    // other Func's loop gets its own loop nest, with its own bounds, sized
    // by bounds inference -- and by default, those bounds are an interval
    // expression in terms of the consumer's loop variables and Params, not
    // a literal constant.
    {
        Var c("c");
        Var x("x"), xo("xo"), xi("xi");
        Var y("y"), yo("yo"), yi("yi");
        Func f("f"), R("R"), G("G"), B("B");
        Param<int> offset_x("offset_x"), offset_y("offset_y");
        offset_x.set_range(0, 2);
        offset_y.set_range(0, 2);
        Expr selector = 3 * ((y - offset_y) % 3) + (x - offset_x) % 3;
        std::vector<Expr> ways;
        for (int i = 0; i < 9; i++) {
            ways.push_back(x * (i % 3) * 3 + y * (i % 3));
        }
        R(x, y) = mux(selector, ways);
        G(x, y) = mux(selector, ways);
        B(x, y) = mux(selector, ways);
        // f itself picks between the three channels with a second mux,
        // indexed by c -- but c's range is a plain compile-time constant
        // set by .bound(), so that one folds regardless of anything to do
        // with alignment.
        f(x, y, c) = mux(c, {R(x, y), G(x, y), B(x, y)});
        f.output_buffer().dim(0).set_min(0);
        f.output_buffer().dim(1).set_min(0);
        f.split(x, xo, xi, 3, offset_x, TailStrategy::GuardWithIf)
            .split(y, yo, yi, 3, offset_y, TailStrategy::GuardWithIf)
            .never_partition_all()
            .reorder(c, xi, yi, xo, yo)
            .unroll(xi)
            .unroll(yi)
            .bound(c, 0, 3)
            .unroll(c);

        // Compute each channel per tile of f, and try to unroll its x, y
        // the same way we unrolled xi, yi above:
        for (Func *channel : {&R, &G, &B}) {
            channel->compute_at(f, xo)
                .never_partition_all()
                .unroll(x)
                .unroll(y);
        }

        // This doesn't even compile. Bounds inference gives R, G, and B's
        // own x loop an extent like
        // "min(xo*3 + offset_x + 3, f.extent.0) - max(xo*3 + offset_x, 0)"
        // -- a runtime expression, not a constant -- because by default it
        // only knows the *region required* by this tile of f, not that
        // it's exactly 3 wide. unroll() requires a compile-time-constant
        // extent, so it fails outright, before the question of whether the
        // muxes fold even comes up.
        //
        // Compiling this schedule throws a Halide::CompileError. Halide can
        // be built with error reporting done via abort() instead of C++
        // exceptions (HALIDE_WITH_EXCEPTIONS undefined, e.g. the top-level
        // Makefile's default), so only attempt to catch it when exceptions
        // are actually the reporting mechanism in this build.
#ifdef HALIDE_WITH_EXCEPTIONS
        bool got_expected_error = false;
        try {
            f.compile_to_module({offset_x, offset_y});
        } catch (const Halide::Error &) {
            got_expected_error = true;
        }
        if (!got_expected_error) {
            printf("Expected compiling this schedule to fail\n");
            return 1;
        }
        printf("As expected, compute_at without a fixed extent can't be unrolled.\n");
#else
        printf("Skipping the expected-compile-failure demonstration (built without exceptions).\n");
#endif

        // Fixing just the crash isn't the same as fixing the muxes. Pin the
        // computed and allocated size of each tile with bound_extent() and
        // bound_storage() -- enough to make unroll() legal again -- but
        // stop there, without telling bounds inference anything about
        // *where* that tile starts relative to offset_x/offset_y:
        Func f2("f2"), R2("R2"), G2("G2"), B2("B2");
        R2(x, y) = mux(selector, ways);
        G2(x, y) = mux(selector, ways);
        B2(x, y) = mux(selector, ways);
        f2(x, y, c) = mux(c, {R2(x, y), G2(x, y), B2(x, y)});
        f2.output_buffer().dim(0).set_min(0);
        f2.output_buffer().dim(1).set_min(0);
        f2.split(x, xo, xi, 3, offset_x, TailStrategy::GuardWithIf)
            .split(y, yo, yi, 3, offset_y, TailStrategy::GuardWithIf)
            .never_partition_all()
            .reorder(c, xi, yi, xo, yo)
            .unroll(xi)
            .unroll(yi)
            .bound(c, 0, 3)
            .unroll(c);
        for (Func *channel : {&R2, &G2, &B2}) {
            channel->compute_at(f2, xo)
                .never_partition_all()
                .bound_storage(x, 3)
                .bound_extent(x, 3)
                .bound_storage(y, 3)
                .bound_extent(y, 3)
                .unroll(x)
                .unroll(y);
        }
        Module m4 = f2.compile_to_module({offset_x, offset_y});
        std::cout << "compute_at with a fixed tile size, but no align_bounds: "
                     "the muxes are still there\n"
                   << find_producer(m4, "B2") << "\n";
        Counter c4 = count(m4);
        if (c4.mux_count == 0) {
            printf("Expected some mux() calls to survive without align_bounds\n");
            return 1;
        }
        printf("Fixed-size tile, no align_bounds: %d mux() left.\n", c4.mux_count);
    }

    // Part 5: circumventing it
    //
    // bound_extent()/bound_storage() alone got the schedule to compile, but
    // the printed IR above still shows mux() calls inside B2: pinning the
    // *size* of a tile doesn't tell bounds inference *where* it starts
    // relative to offset_x/offset_y, so x and y inside R/G/B's own
    // definition aren't provably a compile-time-constant distance from
    // offset_x/offset_y the way xi/yi were in Parts 2-3. Func::align_bounds
    // is what supplies that: it constrains a Func's computed min to be
    // congruent to a given remainder modulo a given modulus -- exactly the
    // "phase" information the outer aligned split already has, but that
    // doesn't cross the compute_at boundary on its own.
    {
        Var c("c");
        Var x("x"), xo("xo"), xi("xi");
        Var y("y"), yo("yo"), yi("yi");
        Func f("f"), R("R"), G("G"), B("B");
        Param<int> offset_x("offset_x"), offset_y("offset_y");
        offset_x.set_range(0, 2);
        offset_y.set_range(0, 2);
        Expr selector = 3 * ((y - offset_y) % 3) + (x - offset_x) % 3;
        std::vector<Expr> ways;
        for (int i = 0; i < 9; i++) {
            ways.push_back(x * (i % 3) * 3 + y * (i % 3));
        }
        R(x, y) = mux(selector, ways);
        G(x, y) = mux(selector, ways);
        B(x, y) = mux(selector, ways);
        f(x, y, c) = mux(c, {R(x, y), G(x, y), B(x, y)});
        f.output_buffer().dim(0).set_min(0);
        f.output_buffer().dim(1).set_min(0);
        f.split(x, xo, xi, 3, offset_x, TailStrategy::GuardWithIf)
            .split(y, yo, yi, 3, offset_y, TailStrategy::GuardWithIf)
            .never_partition_all()
            .reorder(c, xi, yi, xo, yo)
            .unroll(xi)
            .unroll(yi)
            .bound(c, 0, 3)
            .unroll(c);

        for (Func *channel : {&R, &G, &B}) {
            channel->compute_at(f, xo)
                .never_partition_all()
                // Fix the computed and allocated size of this tile...
                .bound_storage(x, 3)
                .bound_extent(x, 3)
                // ...*then* tell bounds inference which phase that tile is
                // anchored to, matching the outer split above. Order
                // matters: bound_extent()/bound_storage() need to run
                // first. Do it the other way around --
                // .align_bounds(x, 3, offset_x).bound_extent(x, 3) -- and
                // bounds inference derives too small a region for this
                // stage (a hard bounds-checking failure at run time, e.g.
                // "Bounds given for B in x (from -2 to 0) do not cover
                // required region (from -2 to 3)"), rather than merely
                // failing to simplify. Getting the order right is a
                // correctness requirement here, not just a simplifier
                // nicety.
                .align_bounds(x, 3, offset_x)
                .bound_storage(y, 3)
                .bound_extent(y, 3)
                .align_bounds(y, 3, offset_y)
                // With the tile's region pinned to a known size and a
                // known phase relative to offset_x/offset_y, x and y
                // inside R/G/B's own definition are now related to
                // offset_x/offset_y by a compile-time-constant difference
                // once unrolled -- exactly the relationship Part 2 relied
                // on -- so the muxes inside R, G, and B fold too.
                .unroll(x)
                .unroll(y);
        }

        Module m = f.compile_to_module({offset_x, offset_y});
        std::cout << "compute_at with bound_extent/bound_storage *and* align_bounds: "
                     "clean again\n"
                   << find_producer(m, "B") << "\n";
        Counter cnt = count(m);
        if (cnt.mux_count != 0) {
            printf("Expected every mux() to fold away, found %d left\n", cnt.mux_count);
            return 1;
        }

        // And the pipeline still computes the right answer, for every
        // runtime alignment of the 3x3 pattern.
        const int W = 16, H = 16;
        for (int oy = 0; oy < 3; oy++) {
            for (int ox = 0; ox < 3; ox++) {
                offset_x.set(ox);
                offset_y.set(oy);
                Buffer<int> im = f.realize({W, H, 3});
                for (int cc = 0; cc < 3; cc++) {
                    for (int y = 0; y < H; y++) {
                        for (int x = 0; x < W; x++) {
                            // Bias by 3 so the operands of % stay
                            // non-negative, where C++'s truncated %
                            // agrees with Halide's Euclidean %.
                            int sel = (3 * ((3 + y - oy) % 3) + (3 + x - ox) % 3);
                            int expected = x * (sel % 3) * 3 + y * (sel % 3);
                            if (im(x, y, cc) != expected) {
                                printf("im(%d, %d, %d) = %d instead of %d\n",
                                       x, y, cc, im(x, y, cc), expected);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
        printf("Full compute_at case: %d for loop(s), %d mux() left.\n", cnt.for_count, cnt.mux_count);
    }

    printf("Success!\n");
    return 0;
}
