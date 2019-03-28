#include <iostream>
#include <map>

#include "HalideRuntime.h"

#include "blur2x2.h"
#include "cxx_mangling.h"
#include "pyramid.h"

namespace {

void check(bool b, const char *msg = "Failure!") {
  if (!b) {
    std::cerr << msg << "\n";
    exit(1);
  }
}

struct Info {
    int (*call)(void **);
    const struct halide_filter_metadata_t *md;
    const char * const *kv;
};

// We need to access this before main() is called, so use
// a static initializer to avoid initization-order fiascos
std::map<std::string, Info>& seen_filters() {
  static std::map<std::string, Info> m;
  return m;
}

extern "C" void halide_register_argv_and_metadata(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    const char * const *extra_key_value_pairs) {

    seen_filters()[filter_metadata->name] = Info{
        filter_argv_call,
        filter_metadata,
        extra_key_value_pairs
    };
}

extern "C" const char * const *halide_register_extra_key_value_pairs_blur2x2() {
    return nullptr;
}

extern "C" const char * const *halide_register_extra_key_value_pairs_cxx_mangling() {
    static const char* const r[4] = {
        "key1", "value1",
        nullptr, nullptr
    };
    return r;
}

extern "C" const char * const *halide_register_extra_key_value_pairs_pyramid() {
    static const char* const r[6] = {
        "key1", "value1",
        "key2", "value2",
        nullptr, nullptr
    };
    return r;
}

}  // namespace

int main(int argc, char **argv) {

  check(seen_filters().size() == 3);

  check(seen_filters()["blur2x2"].call == blur2x2_argv);
  check(seen_filters()["blur2x2"].md == blur2x2_metadata());
  check(seen_filters()["blur2x2"].kv == nullptr);

  check(seen_filters()["cxx_mangling"].call == HalideTest::AnotherNamespace::cxx_mangling_argv);
  check(seen_filters()["cxx_mangling"].md == HalideTest::AnotherNamespace::cxx_mangling_metadata());
  check(seen_filters()["cxx_mangling"].kv != nullptr);
  check(seen_filters()["cxx_mangling"].kv[0] == std::string("key1"));
  check(seen_filters()["cxx_mangling"].kv[1] == std::string("value1"));
  check(seen_filters()["cxx_mangling"].kv[2] == nullptr);
  check(seen_filters()["cxx_mangling"].kv[3] == nullptr);

  check(seen_filters()["pyramid"].call == pyramid_argv);
  check(seen_filters()["pyramid"].md == pyramid_metadata());
  check(seen_filters()["pyramid"].kv != nullptr);
  check(seen_filters()["pyramid"].kv[0] == std::string("key1"));
  check(seen_filters()["pyramid"].kv[1] == std::string("value1"));
  check(seen_filters()["pyramid"].kv[2] == std::string("key2"));
  check(seen_filters()["pyramid"].kv[3] == std::string("value2"));
  check(seen_filters()["pyramid"].kv[4] == nullptr);
  check(seen_filters()["pyramid"].kv[5] == nullptr);

  std::cout << "Success!\n";
  return 0;
}
