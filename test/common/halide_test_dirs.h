#ifndef HALIDE_TEST_DIRS_H
#define HALIDE_TEST_DIRS_H

// This file may be used by AOT tests, so it deliberately does not
// include Halide.h

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Halide {
namespace Internal {

/** Return the path to a directory that can be safely written to
 * when running tests; the contents directory may or may not outlast
 * the lifetime of test itself (ie, the files may be cleaned up after test
 * execution). The path is guaranteed to be an absolute path and end in
 * a directory separator, so a leaf filename can simply be appended. It
 * is not guaranteed that this directory will be empty. If the path cannot
 * be created, the function will assert-fail and return an invalid path.
 */
inline std::string get_test_tmp_dir(const char *subdir = "halide_test") {
#ifdef _WIN32
    char tmp_path[MAX_PATH];
    DWORD ret = GetTempPathA(MAX_PATH, tmp_path);
    internal_assert(ret != 0);
    std::string dir = std::string(tmp_path) + subdir;
    BOOL result = CreateDirectoryA(dir.c_str(), nullptr);
    if (!result && GetLastError() != ERROR_ALREADY_EXISTS) {
        assert(!"Could not create temp dir.");
        return "Z:\\UnlikelyPath\\";
    }
    if (dir[dir.size()-1] != '\\') {
        dir += "\\";
    }
    return dir;
#else
    std::string dir = std::string("/tmp/") + subdir;
    struct stat a;
    if (::stat(dir.c_str(), &a) == 0) {
        if (!S_ISDIR(a.st_mode)) {
            assert(!"Could not create temp dir (a file of that name exists)");
            return "/unlikely_path/";
        }
        // Just assume the permissions are OK.
    } else {
        // We need executable permissions for this folder.
        if (mkdir(dir.c_str(), 0777) != 0) {
            assert(!"Could not create temp dir (error occurred)");
            return "/unlikely_path/";
        }
    }

    if (dir[dir.size()-1] != '/') {
        dir += "/";
    }
    return dir;
#endif
}

}  // namespace Halide
}  // namespace Internal

#endif  // HALIDE_TEST_DIRS_H
