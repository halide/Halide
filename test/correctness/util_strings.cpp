#include "Halide.h"

#include <cstdio>

using namespace Halide::Internal;

namespace {

int failures = 0;

template<typename T, typename U>
void check(const T &actual, const U &expected, const char *what) {
    if (!(actual == expected)) {
        std::cout << "FAILED: " << what << ": got \"" << actual
                  << "\", expected \"" << expected << "\"\n";
        failures++;
    }
}

}  // namespace

int main(int argc, char **argv) {
    // starts_with / ends_with
    check(starts_with("hello world", "hello"), true, "starts_with(\"hello world\", \"hello\")");
    check(starts_with("hello world", "world"), false, "starts_with(\"hello world\", \"world\")");
    check(starts_with("hello", "hello world"), false, "starts_with(\"hello\", \"hello world\") (prefix longer than str)");
    check(starts_with("hello", ""), true, "starts_with(\"hello\", \"\") (empty prefix)");
    check(ends_with("hello world", "world"), true, "ends_with(\"hello world\", \"world\")");
    check(ends_with("hello world", "hello"), false, "ends_with(\"hello world\", \"hello\")");
    check(ends_with("world", "hello world"), false, "ends_with(\"world\", \"hello world\") (suffix longer than str)");
    check(ends_with("hello", ""), true, "ends_with(\"hello\", \"\") (empty suffix)");

    // replace_all
    check(replace_all("aXbXc", "X", "_"), "a_b_c", "replace_all(\"aXbXc\", \"X\", \"_\")");
    check(replace_all("abc", "z", "_"), "abc", "replace_all(\"abc\", \"z\", \"_\") (needle absent)");
    check(replace_all("abc", "", "_"), "abc", "replace_all(\"abc\", \"\", \"_\") (empty needle must be a no-op, not loop forever)");
    check(replace_all("aXXa", "X", "long"), "alonglonga", "replace_all(\"aXXa\", \"X\", \"long\") (replacement longer than needle)");

    // split_string
    {
        std::vector<std::string> want = {"a", "b", "c"};
        check(split_string("a,b,c", ",") == want, true, "split_string(\"a,b,c\", \",\")");
    }
    {
        std::vector<std::string> want = {"", "a", "b"};
        check(split_string(",a,b", ",") == want, true, "split_string(\",a,b\", \",\") (leading delimiter)");
    }
    {
        std::vector<std::string> want = {"a", "b", ""};
        check(split_string("a,b,", ",") == want, true, "split_string(\"a,b,\", \",\") (trailing delimiter)");
    }
    {
        std::vector<std::string> want = {"abc"};
        check(split_string("abc", ",") == want, true, "split_string(\"abc\", \",\") (delimiter absent)");
    }
    {
        // An empty delimiter must be treated as "no delimiter found", not loop forever.
        std::vector<std::string> want = {"abc"};
        check(split_string("abc", "") == want, true, "split_string(\"abc\", \"\") (empty delimiter)");
    }

    // extract_namespaces / strip_namespaces
    {
        std::vector<std::string> namespaces;
        std::string base = extract_namespaces("A::B::C", namespaces);
        std::vector<std::string> want = {"A", "B"};
        check(base, "C", "extract_namespaces(\"A::B::C\") base name");
        check(namespaces == want, true, "extract_namespaces(\"A::B::C\") namespaces");
    }
    {
        std::vector<std::string> namespaces;
        std::string base = extract_namespaces("C", namespaces);
        check(base, "C", "extract_namespaces(\"C\") base name (no namespaces)");
        check(namespaces.empty(), true, "extract_namespaces(\"C\") namespaces (no namespaces)");
    }
    check(strip_namespaces("A::B::C"), "C", "strip_namespaces(\"A::B::C\")");
    check(strip_namespaces("C"), "C", "strip_namespaces(\"C\") (no namespaces)");

    // c_print_name
    check(c_print_name(""), "", "c_print_name(\"\") (must not index name[0] on empty input)");
    check(c_print_name("hello"), "_hello", "c_print_name(\"hello\") (leading letter gets underscore-prefixed)");
    check(c_print_name("9abc"), "9abc", "c_print_name(\"9abc\") (leading digit is not underscore-prefixed)");
    check(c_print_name("a.b"), "_a_b", "c_print_name(\"a.b\") ('.' becomes '_')");
    check(c_print_name("a$b"), "_a__b", "c_print_name(\"a$b\") ('$' becomes '__')");
    // A raw high-bit byte must not be passed to isalpha()/isalnum() as a
    // (possibly negative) plain char -- that's UB. Cast to unsigned char
    // first; with that fix, this deterministically hashes to "___" (not
    // alnum in the "C" locale) rather than crashing or reading out of
    // bounds of an internal ctype table under a hardened libc/ASan build.
    check(c_print_name(std::string(1, '\xFF')), "___", "c_print_name(non-ASCII byte)");

    // running_program_name
    {
        std::string name = running_program_name();
        check(!name.empty(), true, "running_program_name() is non-empty");
        check(name.find('/') == std::string::npos, true, "running_program_name() contains no '/'");
        check(name.find('\\') == std::string::npos, true, "running_program_name() contains no '\\'");
    }

    if (failures > 0) {
        std::cout << failures << " check(s) failed.\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
