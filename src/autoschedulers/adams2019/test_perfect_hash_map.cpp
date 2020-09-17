#include <time.h>

#include <iostream>
#include <map>
#include <random>
#include <vector>

#include "PerfectHashMap.h"

using std::map;
using std::vector;

struct Key {
    int id, max_id;
    Key(int i, int m)
        : id(i), max_id(m) {
    }
};

int main(int argc, char **argv) {
    std::mt19937 rng(0);
    int seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(seed);
    printf("seed: %d\n", seed);

    PerfectHashMap<Key, int> h;
    std::map<const Key *, int> ref;

    std::vector<Key> keys;
    const int N = 100;

    for (int i = 0; i < N; i++) {
        keys.emplace_back(i, N);
    }
    std::shuffle(keys.begin(), keys.end(), rng);

    for (int i = 0; i < 10000; i++) {
        // Insert. Possibly a duplicate of an existing item.
        int next = rng() % N;
        h.insert(&keys[next], next);
        ref.insert({&keys[next], next});

        // Check the map and hash map contain the same stuff in the same order
        if (h.size() != ref.size()) {
            fprintf(stderr, "Size mismatch: %d vs %d\n", (int)h.size(), (int)ref.size());
            return -1;
        }
        // Use iterators to convert PerfectHashMap to map and compare to reference map
        decltype(ref) h_map;
        for (auto it = h.begin(); it != h.end(); it++) {
            h_map.insert({it.key(), it.value()});
        }

        auto it = h_map.begin();
        auto ref_it = ref.begin();
        while (it != h_map.end()) {
            if (it->first != ref_it->first) {
                fprintf(stderr, "Key mismatch: %p vs %p\n", (const void *)it->first, (const void *)ref_it->first);
                return -1;
            }
            if (it->second != ref_it->second) {
                fprintf(stderr, "Value mismatch: %d vs %d\n", it->second, ref_it->second);
                return -1;
            }
            it++;
            ref_it++;
        }
    }
    printf("Perfect hash map test passed\n");
    return 0;
}
