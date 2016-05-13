#include "StaticLibrary.h"

#include <fstream>
#include <stdio.h>

#include "Error.h"

namespace Halide {
namespace Internal {

namespace {

std::string pad_right(std::string s, size_t max) {
    internal_assert(s.size() <= max) << s.size() << " " << s;
    while (s.size() < max) {
        s += " ";
    }
    return s;
}

std::string decimal_string(int value, size_t pad) {
    return pad_right(std::to_string(value), pad);
}

std::string octal_string(int value, size_t pad) {
    char buf[256];
    #ifdef _MSC_VER
    _snprintf(buf, sizeof(buf), "%o", value);
    #else
    snprintf(buf, sizeof(buf), "%o", value);
    #endif
    return pad_right(buf, pad);
}

}  // namespace

void create_ar_file(const std::vector<std::string> &src_files, 
                    const std::string &dst_file, bool deterministic) {
    std::ofstream ar(dst_file, std::ofstream::out | std::ofstream::binary);
    ar << "!<arch>\x0A";
    for (const std::string &src_path : src_files) {
        FileStat stat = file_stat(src_path);

        // Each member must begin on an even byte boundary; insert LF as needed
        if (ar.tellp() & 1) {
            ar << "\x0A";
        }
        // Need to embed just the leaf name
        std::string src_name = base_name(src_path, '/');
        uint64_t filesize = stat.file_size;
        if (src_name.size() > 16) {
            ar << "#1/" << decimal_string(src_name.size(), 13);
            filesize += src_name.size();
        } else {
            ar << pad_right(src_name, 16);
        }
        ar << decimal_string(deterministic ? 0 : stat.mod_time, 12);  // mod time
        ar << decimal_string(deterministic ? 0 : stat.uid, 6);  // user id
        ar << decimal_string(deterministic ? 0 : stat.gid, 6);  // group id
        ar << octal_string(deterministic ? 0644 : stat.mode, 8);  // mode
        ar << decimal_string(filesize, 10);  // filesize
        ar << "\x60\x0A";  // magic
        if (src_name.size() > 16) {
            ar << src_name;
        }
        {
            std::ifstream src(src_path, std::ifstream::in | std::ifstream::binary);
            ar << src.rdbuf();
        }
    }
    user_assert(ar.good());
}

void static_library_test() {
    {
        std::ofstream a("a.tmp", std::ofstream::out);
        a << "a123b";
        user_assert(a.good());
    }
    {
        std::ofstream b("b_long_name_is_long.tmp", std::ofstream::out);
        b << "c456d";
        user_assert(b.good());
    }
    {
        std::ofstream c("./c_path.tmp", std::ofstream::out);
        c << "e789f";
        user_assert(c.good());
    }

    create_ar_file({"a.tmp", "b_long_name_is_long.tmp", "./c_path.tmp"}, "arfile.a");

    {
        std::ifstream ar("arfile.a", std::ifstream::in);
        std::ostringstream actual;
        actual << ar.rdbuf();
        const std::string expected = R"literal(!<arch>
a.tmp           0           0     0     644     5         `
a123b
#1/23           0           0     0     644     28        `
b_long_name_is_long.tmpc456dc_path.tmp      0           0     0     644     5         `
e789f)literal";
        internal_assert(actual.str() == expected) 
            << "File contents wrong, expected:(" << expected << ")\nactual:(" << actual.str() << ")\n";
    }
    file_unlink("a.tmp");
    file_unlink("b_long_name_is_long.tmp");
    file_unlink("./c_path.tmp");
    file_unlink("arfile.a");

    debug(0) << "static_library_test passed\n";
}

}  // namespace Internal
}  // namespace Halide
