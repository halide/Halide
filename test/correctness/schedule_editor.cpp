#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

// Build a small two-stage separable blur over a synthetic input. Returns the
// pipeline's output Func; the intermediates (blur_x, input) are reachable from
// it and get discovered automatically. Rebuilt fresh each call so the editor
// can re-materialize onto unscheduled Funcs.
std::vector<Func> make_blur() {
    Var x("x"), y("y");
    Func input("input"), blur_x("blur_x"), blur_y("blur_y");
    input(x, y) = x * 2 + y;
    blur_x(x, y) = (input(x - 1, y) + input(x, y) + input(x + 1, y)) / 3;
    blur_y(x, y) = (blur_x(x, y - 1) + blur_x(x, y) + blur_x(x, y + 1)) / 3;
    return {blur_y};
}

bool equal(const Buffer<int> &a, const Buffer<int> &b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            if (a(x, y) != b(x, y)) {
                printf("Mismatch at (%d, %d): %d vs %d\n", x, y, a(x, y), b(x, y));
                return false;
            }
        }
    }
    return true;
}

const int W = 123, H = 45;

// A reference result computed with the default (all-inline) schedule.
Buffer<int> reference() {
    return make_blur()[0].realize({W, H});
}

// Reference for the trivial f(x, y) = x + y pipeline used by some tests.
Buffer<int> reference_xy() {
    Var x("x"), y("y");
    Func f("f_xy_ref");
    f(x, y) = x + y;
    return f.realize({W, H});
}

// Build a schedule with the fluent recorder, apply it, and confirm the
// result is unchanged from the reference.
void test_build_and_apply() {
    std::vector<Func> funcs = make_blur();

    ScheduleEditor editor(funcs);
    editor.schedule("blur_y")
        .split("y", "yo", "yi", 16)
        .reorder({"x", "yi", "yo"})
        .parallel("yo")
        .vectorize("x", 8);
    editor.schedule("blur_x")
        .compute_at("blur_y", "yo")
        .vectorize("x", 8);

    editor.apply();
    Buffer<int> out = funcs[0].realize({W, H});

    assert(equal(out, reference()) && "build+apply changed the result");
    assert(editor.size() == 6);
    printf("test_build_and_apply: ok (%zu directives)\n", editor.size());
}

// Edit the directive list (insert / remove / move) and re-materialize a
// fresh pipeline via the factory. Result must still match the reference.
void test_edit_and_materialize() {
    ScheduleEditor editor(make_blur);  // factory ctor
    editor.schedule("blur_y")
        .split("y", "yo", "yi", 8)
        .parallel("yo")
        .vectorize("x", 8);
    editor.schedule("blur_x").compute_root().vectorize("x", 8);

    const size_t original_size = editor.size();

    // INSERT a reorder right after blur_y's split.
    size_t split_idx = editor.find("blur_y").front();
    editor.insert(split_idx + 1,
                  ScheduleDirective::reorder("blur_y", {"x", "yi", "yo"}));
    assert(editor.size() == original_size + 1);

    // REMOVE every vectorize directive.
    size_t removed = editor.remove_matching([](const ScheduleDirective &d) {
        return d.kind == ScheduleDirective::Kind::Vectorize;
    });
    assert(removed == 2);
    assert(editor.size() == original_size - 1);

    // MOVE blur_x's compute_root to the very front.
    size_t cr_idx = editor.find("blur_x").front();
    editor.move(cr_idx, 0);
    assert(editor[0].kind == ScheduleDirective::Kind::ComputeRoot);

    // Materialize a fresh, scheduled pipeline and check correctness.
    Pipeline p = editor.materialize();
    Buffer<int> out = p.realize({W, H});
    assert(equal(out, reference()) && "edit+materialize changed the result");

    // Re-materializing again yields the same result (edits are reproducible).
    Buffer<int> out2 = editor.materialize().realize({W, H});
    assert(equal(out2, reference()));

    printf("test_edit_and_materialize: ok\n");
    printf("---- emitted schedule ----\n%s", ScheduleAnalyzer::to_source(editor.directives()).c_str());
    printf("--------------------------\n");
}

// Schedule an update definition (stage != 0), exercising the Stage dispatch
// path and the fluent .update() retargeting.
void test_update_stage() {
    Var x("x"), y("y");
    RDom r(0, 4);
    Func input("input"), sum("sum");
    input(x, y) = x + y;
    sum(x, y) = 0;
    sum(x, y) += input(x + r, y);

    ScheduleEditor editor({sum, input});
    editor.schedule("sum").compute_root();
    editor.schedule("sum").update(0).reorder({"x", "y"}).vectorize("x", 4);
    editor.apply();

    Buffer<int> out = sum.realize({W, H});

    // Reference.
    Func input2("input"), sum2("sum2");
    input2(x, y) = x + y;
    sum2(x, y) = 0;
    sum2(x, y) += input2(x + r, y);
    Buffer<int> ref = sum2.realize({W, H});

    assert(equal(out, ref) && "update-stage schedule changed the result");

    // The update directives target stage 1 (== update(0)).
    auto updates = editor.find("sum", /*stage=*/1);
    assert(updates.size() == 2);
    printf("test_update_stage: ok\n");
}

// Partition, estimate, and storage directives applied on a CPU target. These
// are hints, so the result must be unchanged from the reference.
void test_partition_and_estimate_directives() {
    std::vector<Func> funcs = make_blur();

    ScheduleEditor editor(funcs);
    editor.schedule("blur_y")
        .split("y", "yo", "yi", 8)
        .partition("yo", Partition::Never)
        .never_partition({"yi"})
        .set_estimate("x", 0, W)
        .set_estimate("y", 0, H)
        .vectorize("x", 8)
        .no_profiling();
    editor.schedule("blur_x")
        .compute_at("blur_y", "yo")
        .align_storage("x", 8)
        .vectorize("x", 8);

    editor.apply();
    Buffer<int> out = funcs[0].realize({W, H});
    assert(equal(out, reference()) && "schedule changed the result");
    printf("test_partition_and_estimate_directives: ok\n");
}

// Directives needing a GPU/Hexagon/trace target aren't applied here, but we
// exercise their construction and to_source() rendering.
void test_to_source() {
    ScheduleEditor editor;
    editor.schedule("f")
        .gpu_tile("x", "bx", "tx", 8)
        .gpu({"x"}, {"tx"})
        .gpu_single_thread()
        .hexagon()
        .trace_loads()
        .add_trace_tag("mytag")
        .never_partition_all()
        .bound_extent("x", 64);

    std::string src = ScheduleAnalyzer::to_source(editor.directives());
    assert(src.find("f.gpu(x, tx);") != std::string::npos);
    assert(src.find("f.gpu_single_thread();") != std::string::npos);
    assert(src.find("f.hexagon();") != std::string::npos);
    assert(src.find("f.trace_loads();") != std::string::npos);
    assert(src.find("f.add_trace_tag(\"mytag\");") != std::string::npos);
    assert(src.find("f.never_partition_all();") != std::string::npos);
    assert(src.find("f.bound_extent(x, 64);") != std::string::npos);
    printf("test_to_source: ok\n");
}

// compute_with (loop fusion) and prefetch. Both preserve results.
void test_compute_with_and_prefetch() {
    Var x("x"), y("y");

    // compute_with: fuse g's loop nest with f's.
    {
        Func f("f"), g("g"), out("out");
        f(x, y) = x + y;
        g(x, y) = x - y;
        out(x, y) = f(x, y) + g(x, y);

        std::vector<Func> outs = {out};
        ScheduleEditor editor(outs);
        editor.schedule("f").compute_root();
        editor.schedule("g").compute_root().compute_with("f", "x");
        editor.apply();
        Buffer<int> res = out.realize({W, H});

        Func f2("f"), g2("g"), out2("out2");
        f2(x, y) = x + y;
        g2(x, y) = x - y;
        out2(x, y) = f2(x, y) + g2(x, y);
        assert(equal(res, out2.realize({W, H})) && "compute_with changed the result");
    }

    // prefetch: a hint; results must be unchanged.
    {
        std::vector<Func> funcs = make_blur();
        ScheduleEditor editor(funcs);
        editor.schedule("blur_x").compute_root();
        editor.schedule("blur_y").prefetch("blur_x", "y", "y", 8);
        editor.apply();
        Buffer<int> out = funcs[0].realize({W, H});
        assert(equal(out, reference()) && "prefetch changed the result");
    }

    printf("test_compute_with_and_prefetch: ok\n");
}

// specialize() records a nested schedule scope; the result is
// unchanged whichever branch runs.
void test_specialize() {
    Var x("x"), y("y");
    Param<bool> fast;
    Func f("f");
    f(x, y) = x + y;

    ScheduleEditor editor({f});
    editor.schedule("f").specialize(fast).vectorize("x", 8);
    editor.apply();

    fast.set(true);
    Buffer<int> out = f.realize({W, H});
    assert(equal(out, reference_xy()) && "specialize changed the result");

    // The specialize scope is reflected in the emitted source.
    std::string src = ScheduleAnalyzer::to_source(editor.directives());
    assert(src.find(".specialize(") != std::string::npos);
    assert(src.find(".vectorize(x, 8);") != std::string::npos);
    printf("test_specialize: ok\n");
}

// rfactor() splits an associative reduction into an intermediate
// Func, referenced by the alias we give it. Result is preserved. Reduction
// vars must be captured from the actual RDom (their names are uniquified), so
// this path targets an existing pipeline rather than a factory rebuild.
void test_rfactor() {
    Var x("x"), y("y"), u("u");
    RVar ro("ro"), ri("ri");
    Func in("in"), f("f");
    in(x, y) = x + y;
    RDom r(0, 16);
    f(x, y) = 0;
    f(x, y) += in(x + r.x, y);

    ScheduleEditor editor({f});
    editor.schedule("f")
        .update(0)
        .split(r.x, ro, ri, 4)
        .rfactor({{VarSpec(ro), VarSpec(u)}}, "f_intm")
        .compute_root();
    editor.apply();
    Buffer<int> out = f.realize({W, H});

    Func in2("in"), f2("f2");
    in2(x, y) = x + y;
    RDom r2(0, 16);
    f2(x, y) = 0;
    f2(x, y) += in2(x + r2.x, y);
    assert(equal(out, f2.realize({W, H})) && "rfactor changed the result");
    printf("test_rfactor: ok\n");
}

// in() inserts a wrapper Func (referenced by its alias) and preserves the
// result; clone_in()'s rendering is checked via to_source().
void test_in_and_clone_in() {
    Var x("x"), y("y");
    Func f("f"), g("g");
    f(x, y) = x + y;
    g(x, y) = f(x, y) * 2;

    ScheduleEditor editor({g});
    editor.schedule("f").compute_root();
    editor.schedule("f").in({"g"}, "f_in").compute_at("g", "y").vectorize("x", 8);
    editor.apply();
    Buffer<int> out = g.realize({W, H});

    Func f2("f"), g2("g2");
    f2(x, y) = x + y;
    g2(x, y) = f2(x, y) * 2;
    assert(equal(out, g2.realize({W, H})) && "in() changed the result");

    // clone_in construction + rendering (not applied here).
    ScheduleEditor other;
    other.schedule("f").clone_in({"g"}, "f_clone").compute_root();
    assert(ScheduleAnalyzer::to_source(other.directives()).find("clone_in(g) /* -> f_clone */;") !=
           std::string::npos);
    printf("test_in_and_clone_in: ok\n");
}

// Serialize a schedule to JSON, deserialize it, and apply the loaded schedule
// to an existing pipeline. The result must match the reference.
void test_json() {
    ScheduleEditor authoring(make_blur());
    authoring.schedule("blur_y").split("y", "yo", "yi", 16).parallel("yo").vectorize("x", 8);
    authoring.schedule("blur_x").compute_at("blur_y", "yo").vectorize("x", 8);

    ScheduleAnalyzer authored(authoring.directives());
    std::string json = authored.to_json();

    // Round-trip: the parsed directives reproduce the same source.
    ScheduleDirectives loaded = ScheduleAnalyzer::from_json(json);
    assert(loaded.size() == authoring.size());
    assert(ScheduleAnalyzer::to_source(loaded) == authored.to_source() &&
           "json round-trip changed the schedule");

    // An editor can also be seeded directly from an existing directive list.
    ScheduleEditor from_list(authoring.directives());
    assert(from_list.size() == authoring.size());
    assert(ScheduleAnalyzer::to_source(from_list.directives()) == authored.to_source());

    // Apply the loaded JSON schedule to an existing pipeline.
    std::vector<Func> funcs = make_blur();
    ScheduleEditor(loaded).apply(funcs);
    Buffer<int> out = funcs[0].realize({W, H});
    assert(equal(out, reference()) && "applying a json schedule changed the result");
    printf("test_json: ok\n");
}

// ScheduleValidator statically catches missing funcs, missing vars, and bad
// stage indices without applying anything.
void test_validator() {
    std::vector<Func> funcs = make_blur();

    // A valid schedule: reorder references vars that the split has introduced.
    ScheduleEditor good(funcs);
    good.schedule("blur_y").split("y", "yo", "yi", 8).reorder({"x", "yi", "yo"}).parallel("yo");
    good.schedule("blur_x").compute_at("blur_y", "yo");
    ScheduleValidator vg(good.directives(), funcs);
    assert(vg.is_valid() && "valid schedule flagged invalid");
    assert(vg.issues().empty());

    // An invalid schedule: unknown Func, unknown vars, a bad loop level.
    ScheduleEditor bad(funcs);
    bad.schedule("nonexistent").vectorize("x", 8);
    bad.schedule("blur_y").vectorize("z", 8);
    bad.schedule("blur_y").split("y", "yo", "yi", 8).reorder({"x", "missing", "yo"});
    bad.schedule("blur_x").compute_at("blur_y", "nope");
    ScheduleValidator vb(bad.directives(), funcs);
    assert(!vb.is_valid());

    std::vector<std::string> mf = vb.missing_funcs();
    assert(std::find(mf.begin(), mf.end(), "nonexistent") != mf.end());

    std::vector<std::pair<std::string, std::string>> mv = vb.missing_vars();
    bool z = false, missing = false, nope = false;
    for (const auto &p : mv) {
        z = z || p.second == "z";
        missing = missing || p.second == "missing";
        nope = nope || p.second == "nope";
    }
    assert(z && missing && nope && "expected missing vars z, missing, nope");

    using K = ScheduleValidator::Issue::Kind;
    auto has_kind = [&](const ScheduleValidator &v, K k) {
        for (const auto &iss : v.issues()) {
            if (iss.kind == k) {
                return true;
            }
        }
        return false;
    };

    // Out-of-range stage.
    ScheduleEditor stage(funcs);
    stage.schedule("blur_y", 3).vectorize("x", 8);
    assert(has_kind(ScheduleValidator(stage.directives(), funcs), K::InvalidStage));

    // Non-positive split factor.
    ScheduleEditor factor(funcs);
    factor.schedule("blur_y").split("y", "yo", "yi", 0);
    assert(has_kind(ScheduleValidator(factor.directives(), funcs), K::BadFactor));

    // The same var named twice in a reorder.
    ScheduleEditor dup(funcs);
    dup.schedule("blur_y").reorder({"x", "x"});
    assert(has_kind(ScheduleValidator(dup.directives(), funcs), K::DuplicateVar));

    // A newly-introduced var shadows an existing one (outer name reuses 'x').
    ScheduleEditor collide(funcs);
    collide.schedule("blur_y").split("y", "x", "yi", 8);
    assert(has_kind(ScheduleValidator(collide.directives(), funcs), K::VarCollision));

    // Malformed: a split with the wrong number of operands.
    ScheduleEditor malformed(funcs);
    ScheduleDirective d;
    d.kind = ScheduleDirective::Kind::Split;
    d.func = "blur_y";
    d.vars = {VarSpec("y"), VarSpec("yo")};  // split needs 3 vars
    d.exprs = {Expr(8)};
    malformed.append(d);
    assert(has_kind(ScheduleValidator(malformed.directives(), funcs), K::Malformed));

    printf("test_validator: ok\n");
}

// ScheduleEditor is an IntrusivePtr handle: copies and the StageHandles it hands
// out share the same directive list, and a handle keeps that list alive -- so
// there is no dangling-handle hazard to guard against.
void test_shared_contents() {
    ScheduleEditor a;
    a.schedule("f").compute_root();

    ScheduleEditor b = a;  // shares the same directive list
    b.schedule("f").vectorize("x", 8);
    assert(a.size() == 2 && b.size() == 2 && "editor copies share the directive list");

    // A handle taken from an editor that then goes out of scope stays valid,
    // because it holds the shared list alive.
    StageHandle h = [] {
        ScheduleEditor tmp;
        tmp.schedule("g").compute_root();
        return tmp.schedule("g");
    }();
    h.vectorize("x", 4);
    assert(h.editor().size() == 2 && "handle keeps the shared list alive and editable");
    printf("test_shared_contents: ok\n");
}

// ScheduleAnalyzer reconstructs an existing schedule as directives and answers
// queries about the Funcs and vars it found.
void test_analyzer() {
    Var x("x"), y("y"), yo("yo"), yi("yi");

    // Schedule a blur directly (not through the editor), then analyze it.
    Func input("input"), blur_x("blur_x"), blur_y("blur_y");
    input(x, y) = x * 2 + y;
    blur_x(x, y) = (input(x - 1, y) + input(x, y) + input(x + 1, y)) / 3;
    blur_y(x, y) = (blur_x(x, y - 1) + blur_x(x, y) + blur_x(x, y + 1)) / 3;
    blur_y.split(y, yo, yi, 8).parallel(yo).vectorize(x, 8);
    blur_x.compute_at(blur_y, yo).vectorize(x, 8);

    ScheduleAnalyzer analyzer({blur_y});

    // Queries by name.
    assert(analyzer.has_func("blur_y") && analyzer.has_func("blur_x") && analyzer.has_func("input"));
    assert(!analyzer.has_func("nope"));
    assert(analyzer.stage_count("blur_y") == 1);
    assert(analyzer.has_var("blur_y", "yo") && analyzer.has_var("blur_y", "yi"));

    // Query by usage.
    assert(!analyzer.directive_indices_for("blur_x").empty());

    // The reconstruction captured the key directives.
    std::string src = analyzer.to_source();
    assert(src.find("blur_y.split(y, yo, yi, 8)") != std::string::npos);
    assert(src.find("blur_x.compute_at(blur_y, yo)") != std::string::npos);

    // Round-trip: re-applying the reconstructed directives to a fresh, identical
    // pipeline reproduces the reference result.
    std::vector<Func> fresh = make_blur();
    ScheduleEditor editor(analyzer.directives());
    editor.apply(fresh);
    Buffer<int> out = fresh[0].realize({W, H});
    assert(equal(out, reference()) && "reconstructed schedule changed the result");

    // An analyzer built from a directive list answers the same name queries.
    ScheduleAnalyzer from_list(analyzer.directives());
    assert(from_list.has_func("blur_y"));
    assert(from_list.directives().size() == analyzer.directives().size());

    // specialize() scopes are recovered: the specialize directive plus the
    // directives scheduled inside it, each carrying the condition path.
    {
        Param<bool> cond;
        Func s("s");
        s(x, y) = x + y;
        s.specialize(cond).vectorize(x, 8);
        s.specialize_fail("no default schedule");

        ScheduleAnalyzer sa({s});
        bool has_specialize = false, has_fail = false, nested_under_condition = false;
        for (const ScheduleDirective &d : sa.directives()) {
            if (d.kind == ScheduleDirective::Kind::Specialize) {
                has_specialize = true;
            }
            if (d.kind == ScheduleDirective::Kind::SpecializeFail) {
                has_fail = true;
            }
            if (d.kind == ScheduleDirective::Kind::Vectorize && !d.specialize_conditions.empty()) {
                nested_under_condition = true;
            }
        }
        assert(has_specialize && "specialize() was not reconstructed");
        assert(has_fail && "specialize_fail() was not reconstructed");
        assert(nested_under_condition && "vectorize inside specialize lost its condition path");
    }

    printf("test_analyzer: ok\n");
}

void run_all_tests() {
    test_build_and_apply();
    test_edit_and_materialize();
    test_update_stage();
    test_partition_and_estimate_directives();
    test_to_source();
    test_compute_with_and_prefetch();
    test_specialize();
    test_rfactor();
    test_in_and_clone_in();
    test_json();
    test_validator();
    test_analyzer();
    test_shared_contents();
}

}  // namespace

int main(int argc, char **argv) {
#ifdef HALIDE_WITH_EXCEPTIONS
    try {
        run_all_tests();
    } catch (const Halide::Error &e) {
        printf("Halide error: %s\n", e.what());
        return 1;
    }
#else
    run_all_tests();
#endif
    printf("Success!\n");
    return 0;
}
