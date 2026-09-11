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
