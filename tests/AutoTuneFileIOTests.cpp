#include "AutoTuneFileIO.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* description) {
    if (!value) throw std::runtime_error(description);
}
}
int main() {
    std::string output = "original value";
    {
        std::istringstream input("config-data");
        check(autotune::readBounded(input, output, 11), "exact length must pass");
        check(output == "config-data", "exact content retained");
    }
    {
        std::istringstream input(std::string(8192, 'x'));
        check(autotune::readBounded(input, output, 8192), "two full buffers must pass");
        check(output.size() == 8192, "full-sized input retained");
    }
    {
        const auto previous = output;
        std::istringstream input(std::string(8193, 'y'));
        check(!autotune::readBounded(input, output, 8192), "growth beyond cap must fail");
        check(output == previous, "no partial bytes should escape on failure");
    }
    {
        std::istringstream input("");
        check(autotune::readBounded(input, output, 0), "empty input with zero cap must pass");
        check(output.empty(), "empty input updates output");
    }
    {
        output = "untouched";
        std::istringstream input("unavailable");
        input.setstate(std::ios::badbit);
        check(!autotune::readBounded(input, output, 128), "I/O error must fail");
        check(output == "untouched", "I/O error must not replace output");
    }
    std::cout << "AutoTuneFileIOTests PASS\n";
}
