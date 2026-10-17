#include "BoundedDiagnosticOutput.h"
#include <iostream>
#include <stdexcept>
#include <string>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    BoundedDiagnosticOutput output;
    check(output.text().empty(), "empty output remains empty");
    output.append("PresentMon error 6: ETW denied\n");
    check(output.text().find("ETW denied") != std::string::npos, "diagnostic text retained");
    output.append(std::string_view("\0\x1b\r\t", 4));
    const auto safe = output.text();
    for (unsigned char c : safe) check(c >= 32, "control characters sanitized for logs/UI");
    output.append(std::string(1024 * 1024, 'x'));
    check(output.size() == BoundedDiagnosticOutput::kCapacity, "retained bytes are capped");
    check(output.truncated(), "overflow is explicit");
    check(output.text().size() <= BoundedDiagnosticOutput::kCapacity + 64, "bounded persisted text");
    check(output.text(100).size() <= 164, "bounded displayed diagnostic");
    check(output.text(100).find("truncated") != std::string::npos, "display truncation explicit");
    output.clear();
    check(output.size() == 0 && !output.truncated() && output.text().empty(), "retry clears stale errors");
    std::cout << "Bounded diagnostic output tests passed\n";
}
