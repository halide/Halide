#include "Halide.h"

#include <algorithm>
#include <cstdio>

using namespace Halide::Internal;

namespace {

int failures = 0;

void check(bool actual, bool expected, const std::string &what) {
    if (actual != expected) {
        std::cout << "FAILED: " << what << ": got " << actual << ", expected " << expected << "\n";
        failures++;
    }
}

void check_round_trip(size_t size) {
    const std::string what = "(size=" + std::to_string(size) + ") ";

    std::string path = file_make_temp("halide_test_util_file_io_", ".bin");
    check(file_exists(path), true, what + "file_make_temp() creates the file");

    // Always back the write with a nonempty allocation (even when size==0)
    // so we never pass a null pointer as the write source.
    std::vector<char> data(std::max<size_t>(size, 1));
    for (size_t i = 0; i < size; i++) {
        data[i] = (char)(i & 0xff);
    }

    write_entire_file(path, data.data(), size);

    FileStat st = file_stat(path);
    check(st.file_size == size, true, what + "file_stat().file_size matches written length");

    std::vector<char> read_back = read_entire_file(path);
    check(read_back.size() == size, true, what + "read_entire_file() returns the written length");
    check(std::equal(read_back.begin(), read_back.end(), data.begin()), true, what + "read_entire_file() returns the written content");

    file_unlink(path);
    check(file_exists(path), false, what + "file_unlink() removes the file");

    // Both should be no-ops (no crash) on an already-removed file.
    assert_no_file_exists(path);
    ensure_no_file_exists(path);
}

}  // namespace

int main(int argc, char **argv) {
    for (size_t size : {(size_t)0, (size_t)1, (size_t)100, (size_t)65536}) {
        check_round_trip(size);
    }

    std::string dir = dir_make_temp();
    check(!dir.empty(), true, "dir_make_temp() returns a non-empty path");
    check(file_exists(dir), true, "dir_make_temp() creates a directory that exists");
    dir_rmdir(dir);
    check(file_exists(dir), false, "dir_rmdir() removes the directory");

    if (failures > 0) {
        std::cout << failures << " check(s) failed.\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
