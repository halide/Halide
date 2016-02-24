#include <iomanip>
#include <sstream>
#include <chrono>

std::string items_per_second(int N, double elapsed) {
    double ips = N * 1000 / elapsed;
    std::string postfix = "";
    if (ips >= 1e8) {
        ips /= 1e9;
        postfix = "G";
    } else if (ips >= 1e5) {
        ips /= 1e6;
        postfix = "M";
    } else if (ips >= 1e2) {
        ips /= 1e3;
        postfix = "k";
    }

    std::ostringstream sout;
    sout << std::setprecision(3) << ips << postfix << "(items/s)";
    return sout.str();
}

double current_time() {
    static auto start_time = std::chrono::system_clock::now().time_since_epoch();
    auto now = std::chrono::system_clock::now().time_since_epoch() - start_time;
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count() / 1e3;
}
