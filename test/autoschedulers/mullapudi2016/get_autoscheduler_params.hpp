#pragma once

#include <Halide.h>

#include <map>
#include <optional>
#include <string>

namespace {

struct Mullapudi2016TestParams {
    unsigned last_level_cache_size{};
    unsigned parallelism{};
};

Halide::AutoschedulerParams get_mullapudi2016_test_params(const bool using_experimental_gpu_schedule, std::optional<Mullapudi2016TestParams> gpu_params = std::nullopt) {
    using std::string;

    std::map<string, string> params{
        {"experimental_gpu_schedule", using_experimental_gpu_schedule ? "1" : "0"}};

    if (using_experimental_gpu_schedule && gpu_params.has_value()) {
        params["last_level_cache_size"] = std::to_string(gpu_params.value().last_level_cache_size);
        params["parallelism"] = std::to_string(gpu_params.value().parallelism);
    }
    return {"Mullapudi2016", params};
}

}  // namespace
