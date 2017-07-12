#ifndef HALIDE_TEST_DIRS_H
#define HALIDE_TEST_DIRS_H

// This file may be used by AOT tests, so it deliberately does not
// include Halide.h

#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Halide {
namespace Internal {

namespace Test {

inline std::string get_env_variable(const char *name) {
#ifdef _MSC_VER
    char buf[MAX_PATH];
    size_t read = 0;
    if (getenv_s(&read, buf, name) != 0) read = 0;
    if (read) {
        return std::string(buf);
    }
#else
    char *buf = getenv(name);
    if (buf) {
        return std::string(buf);
    }
#endif
    return "";
}

// Return absolute path to the current directory. Return empty string if
// an error occurs. (Does not assert.)
inline std::string get_current_directory() {
#ifdef _WIN32
    std::string dir;
    char p[MAX_PATH];
    DWORD ret = GetCurrentDirectoryA(MAX_PATH, p);
    if (ret != 0) {
        dir = p;
    }
    return dir;
#else
    std::string dir;
    char *p = getcwd(nullptr, 0);
    if (p) {
        dir = p;
        free(p);
    }
    return dir;
#endif
}

}  // namespace Test

/** Return the path to a directory that can be safely written to
 * when running tests; the contents directory may or may not outlast
 * the lifetime of test itself (ie, the files may be cleaned up after test
 * execution). The path is guaranteed to be an absolute path and end in
 * a directory separator, so a leaf filename can simply be appended. It
 * is not guaranteed that this directory will be empty. If the path cannot
 * be created, the function will assert-fail and return an invalid path.
 */
inline std::string get_test_tmp_dir() {
    // If TEST_TMPDIR is specified, we assume it is a valid absolute path
    std::string dir = Test::get_env_variable("TEST_TMPDIR");
    if (dir.empty()) {
        // If not specified, use current dir.
        dir = Test::get_current_directory();
    }
    bool is_absolute = dir.size() >= 1 && dir[0] == '/';
    char sep = '/';
#ifdef _WIN32
    // Allow for C:\whatever or c:/whatever on Windows
    if (dir.size() >= 3 && dir[1] == ':' && (dir[2] == '\\' || dir[2] == '/')) {
        is_absolute = true;
        sep = dir[2];
    }
#endif
    if (!is_absolute) {
        assert(false && "get_test_tmp_dir() is not an absolute path");
        return "/unlikely_path/";
    }
    if (dir[dir.size() - 1] != sep) {
        dir += sep;
    }
    return dir;
}

}  // namespace Halide
}  // namespace Internal

#endif  // HALIDE_TEST_DIRS_H
