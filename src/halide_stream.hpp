#ifndef HALIDE_STREAM_H
#define HALIDE_STREAM_H

#include <sstream>

namespace Halide::Internal {
    class CodeGen_C;
    class CodeGen_PyTorch;
    class PythonExtensionGen;
}

struct halide_stream {
    union {
        std::string strstream;
        std::ostringstream private_oss;
    };

    halide_stream();
    halide_stream(std::ostream& s);
    ~halide_stream();

    template<typename T>
    halide_stream& operator << (const T& t);

    halide_stream& operator << (const char* str);
    halide_stream& operator << (char* str);
    halide_stream& operator << (const std::string& str);
    halide_stream& operator << (const unsigned char (&str) []);

    template<size_t N>
    halide_stream& operator << (const char (&str) [N]);

    halide_stream& write(const char* str, std::streamsize count);

    std::string str();

    void setf(std::ios::fmtflags flags, std::ios::fmtflags mask);

private:
    static std::ostringstream sentinel;

    std::ostream& redirect;

    friend class Halide::Internal::CodeGen_C;
    friend class Halide::Internal::CodeGen_PyTorch;
    friend class Halide::Internal::PythonExtensionGen;
};

#endif//HALIDE_STREAM_H
