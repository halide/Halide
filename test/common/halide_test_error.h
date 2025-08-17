#ifndef HALIDE_TEST_ERROR_H
#define HALIDE_TEST_ERROR_H

#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <regex>

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Property;
using ::testing::Throws;

class MatchesStdRegex : public ::testing::MatcherInterface<const std::string &> {
public:
    explicit MatchesStdRegex(const std::string &pattern)
        : regex_pattern_(pattern) {
    }

    bool MatchAndExplain(const std::string &str, ::testing::MatchResultListener *) const override {
        return std::regex_search(str, std::regex(regex_pattern_));
    }

    void DescribeTo(::std::ostream *os) const override {
        *os << "matches regex \"" << regex_pattern_ << "\"";
    }

    void DescribeNegationTo(::std::ostream *os) const override {
        *os << "doesn't match regex \"" << regex_pattern_ << "\"";
    }

private:
    const std::string regex_pattern_;
};

inline ::testing::Matcher<const std::string &> MatchesPattern(const std::string &pattern) {
    return ::testing::MakeMatcher(new MatchesStdRegex(pattern));
}

// Helper macros for testing Halide errors that throw exceptions or call abort()
// depending on whether HALIDE_WITH_EXCEPTIONS is defined

#ifdef HALIDE_WITH_EXCEPTIONS
#define EXPECT_ERROR_TYPE(function_call, error_type, ...) \
    EXPECT_THAT(function_call,                            \
                Throws<error_type>(                       \
                    Property(&Halide::Error::what,        \
                             AllOf(__VA_ARGS__))))
#else
#define EXPECT_ERROR_TYPE(function_call, error_type, ...) \
    EXPECT_DEATH(function_call(), ".*")
#endif

#define EXPECT_COMPILE_ERROR(function_call, ...) \
    EXPECT_ERROR_TYPE(function_call, Halide::CompileError, __VA_ARGS__)

#define EXPECT_RUNTIME_ERROR(function_call, ...) \
    EXPECT_ERROR_TYPE(function_call, Halide::RuntimeError, __VA_ARGS__)

#define EXPECT_INTERNAL_ERROR(function_call, ...) \
    EXPECT_ERROR_TYPE(function_call, Halide::InternalError, __VA_ARGS__)

#endif  // HALIDE_TEST_ERROR_H
