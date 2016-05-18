#include "StaticLibrary.h"

#include <fstream>
#include <stdio.h>

#include "Error.h"

namespace Halide {
namespace Internal {

namespace {

// This is just a simple wrapper that allows us to consume a vector<uint8_t>
// like a string.
template<typename T>
class vector_istreambuf : public std::basic_streambuf<char> {
public:
    vector_istreambuf(const std::vector<T> &vec) {
        static_assert(sizeof(T) == sizeof(char), "T must be sizeof(char)");
        char *p = reinterpret_cast<char*>(const_cast<T*>(vec.data()));
        setg(p, p, p + vec.size());
    }
};

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

void append_ar_file(std::ofstream &ar, const std::string &src_path, const FileStat &src_stat, std::istream &src_data) {
    // Each member must begin on an even byte boundary; insert LF as needed
    if (ar.tellp() & 1) {
        ar << "\x0A";
    }
    // Need to embed just the leaf name
    std::string src_name = base_name(src_path, '/');
    uint64_t filesize = src_stat.file_size;
    if (src_name.size() > 16) {
        ar << "#1/" << decimal_string(src_name.size(), 13);
        filesize += src_name.size();
    } else {
        ar << pad_right(src_name, 16);
    }
    ar << decimal_string(src_stat.mod_time, 12);  // mod time
    ar << decimal_string(src_stat.uid, 6);  // user id
    ar << decimal_string(src_stat.gid, 6);  // group id
    ar << octal_string(src_stat.mode, 8);  // mode
    ar << decimal_string(filesize, 10);  // filesize
    ar << "\x60\x0A";  // magic
    if (src_name.size() > 16) {
        ar << src_name;
    }
    ar << src_data.rdbuf();
}

void write_to(const char *path, const char *data) {
    std::ofstream a(path, std::ofstream::out);
    a << data;
    internal_assert(a.good());
}

std::string read_from(const char *path) {
    std::ifstream a(path, std::ifstream::in);
    std::ostringstream actual;
    actual << a.rdbuf();
    internal_assert(a.good());
    internal_assert(actual.good());
    return actual.str();
}

}  // namespace

void create_ar_file(const std::vector<std::string> &src_files, 
                    const std::string &dst_file, bool deterministic) {
    std::ofstream ar(dst_file, std::ofstream::out | std::ofstream::binary);
    ar << "!<arch>\x0A";
    for (const std::string &src_path : src_files) {
        FileStat src_stat = file_stat(src_path);
        if (deterministic) {
            src_stat.mod_time = 0;
            src_stat.uid = 0;
            src_stat.gid = 0;
            src_stat.mode = 0644;
        }
        std::ifstream src_data(src_path, std::ifstream::in | std::ifstream::binary);
        append_ar_file(ar, src_path, src_stat, src_data);
    }
    user_assert(ar.good());
}

void create_ar_file(const std::vector<ArInput> &src_files, 
                    const std::string &dst_file) {
    std::ofstream ar(dst_file, std::ofstream::out | std::ofstream::binary);
    ar << "!<arch>\x0A";
    for (const ArInput &input : src_files) {
        FileStat src_stat = { input.data.size(), 0, 0, 0, 0644 };
        vector_istreambuf<uint8_t> streambuf(input.data);
        std::istream src_data(&streambuf);
        append_ar_file(ar, input.name, src_stat, src_data);
    }
    user_assert(ar.good());
}

void static_library_test() {

        const std::string expected = R"literal(!<arch>
a.tmp           0           0     0     644     5         `
a123b
#1/23           0           0     0     644     28        `
b_long_name_is_long.tmpc456dc_path.tmp      0           0     0     644     5         `
e789f)literal";

    // Test the file version
    write_to("a.tmp", "a123b");
    write_to("b_long_name_is_long.tmp", "c456d");
    write_to("./c_path.tmp", "e789f");

    create_ar_file({"a.tmp", "b_long_name_is_long.tmp", "./c_path.tmp"}, "arfile.a");

    std::string actual = read_from("arfile.a");
    internal_assert(actual == expected) 
        << "File contents wrong, expected:(" << expected << ")\nactual:(" << actual << ")\n";

    file_unlink("a.tmp");
    file_unlink("b_long_name_is_long.tmp");
    file_unlink("./c_path.tmp");
    file_unlink("arfile.a");

    // Test the memory version
    create_ar_file({
        ArInput{ "a.tmp", {'a', '1', '2', '3', 'b'} },
        ArInput{ "b_long_name_is_long.tmp", {'c', '4', '5', '6', 'd'} },
        ArInput{ "./c_path.tmp", {'e', '7', '8', '9', 'f'} }
    }, "arfile2.a");

    std::string actual2 = read_from("arfile2.a");
    internal_assert(actual2 == expected) 
        << "File contents wrong, expected:(" << expected << ")\nactual:(" << actual2 << ")\n";

    file_unlink("arfile2.a");

    debug(0) << "static_library_test passed\n";
}

}  // namespace Internal
}  // namespace Halide
