#include "Halide.h"
#include <cstdio>

using namespace Halide;

#ifdef TEST_WITH_SERIALIZATION

#include <map>
#include <vector>

namespace {

// A pipeline whose Expr tree nests deeper than flatbuffers::Verifier's
// default max_depth (64), to make sure legitimate pipelines aren't rejected
// by the verifier that guards deserialize().
Pipeline make_deeply_nested_pipeline(Var x, Var y) {
    Expr e = x + y;
    for (int i = 0; i < 300; i++) {
        e = e + i * (x - y);
    }
    Func f("deeply_nested");
    f(x, y) = e;
    return Pipeline(f);
}

}  // namespace

int main() {
    Var x("x"), y("y");

    // A pipeline nested well past the verifier's default table-depth limit
    // round-trips through serialize/deserialize without error.
    {
        Pipeline pipeline = make_deeply_nested_pipeline(x, y);

        std::vector<uint8_t> data;
        std::map<std::string, Parameter> params;
        serialize_pipeline(pipeline, data, params);

        Pipeline deserialized = deserialize_pipeline(data, params);
        Buffer<int> result = deserialized.realize({4, 4});

        Buffer<int> expected = pipeline.realize({4, 4});
        for (int j = 0; j < 4; j++) {
            for (int i = 0; i < 4; i++) {
                if (result(i, j) != expected(i, j)) {
                    printf("Mismatch at (%d, %d): expected %d, got %d\n",
                           i, j, expected(i, j), result(i, j));
                    return 1;
                }
            }
        }
    }

    // storage_splits survive a round trip.
    {
        Func f("f"), g("g");
        Var xo("xo"), xi("xi");
        f(x, y) = x + y;
        g(x, y) = f(x, y);
        f.compute_root().split_storage(x, xo, xi, 4).reorder_storage(xi, y, xo);

        std::vector<uint8_t> data;
        std::map<std::string, Parameter> params;
        serialize_pipeline(Pipeline(g), data, params);
        Pipeline deserialized = deserialize_pipeline(data, params);

        bool found = false;
        std::map<std::string, Internal::Function> env =
            Internal::find_transitive_calls(deserialized.outputs()[0].function());
        for (const auto &p : env) {
            const auto &splits = p.second.schedule().storage_splits();
            if (p.second.name() == f.name() && splits.size() == 1 &&
                splits[0].old_var == x.name() && splits[0].outer == xo.name() &&
                splits[0].inner == xi.name() && is_const(splits[0].factor, 4)) {
                found = true;
            }
        }
        if (!found) {
            printf("storage_splits were not preserved by serialization\n");
            return 1;
        }

        Buffer<int> result = deserialized.realize({10, 6});
        for (int j = 0; j < 6; j++) {
            for (int i = 0; i < 10; i++) {
                if (result(i, j) != i + j) {
                    printf("Split storage mismatch at (%d, %d): got %d\n", i, j, result(i, j));
                    return 1;
                }
            }
        }
    }

    // A corrupted buffer is still rejected: raising max_depth must not
    // disable the structural verification #9395 added.
    {
        Func f("f");
        f(x, y) = x + y;

        std::vector<uint8_t> data;
        std::map<std::string, Parameter> params;
        serialize_pipeline(Pipeline(f), data, params);

        for (size_t i = data.size() / 2; i < data.size() / 2 + 32 && i < data.size(); i++) {
            data[i] ^= 0xff;
        }

        bool rejected = false;
        try {
            deserialize_pipeline(data, params);
        } catch (const Error &) {
            rejected = true;
        }
        if (!rejected) {
            printf("Deserializing a corrupted buffer should have thrown an error\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

#else

int main() {
    printf("[SKIP] Halide was compiled without serialization support.\n");
    return 0;
}

#endif
