#include <cassert>
#include <cstring>
#include <iostream>
#include <regex>

// A utility that does a single regexp-based replace on stdin and dumps it to stdout.
// Exists solely because we can't rely on (e.g.) `sed` being available in Windows
// build environments. Usage is basically equivalent to `sed -e 's/regex/replacement/g'`
// Note that if regex is an empty string, this becomes a simple line-by-line file copy.

int main(int argc, const char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s regex replacement\n", argv[0]);
        return -1;
    }
    std::regex re(argv[1]);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::regex_replace(std::ostreambuf_iterator<char>(std::cout),
                           line.begin(), line.end(), re, argv[2]);
        std::cout << "\n";
    }
    return 0;
}
