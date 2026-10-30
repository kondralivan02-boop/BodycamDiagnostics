#include "GpuEnginePolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    using telemetry::isTargetProcess3DEngine;
    check(isTargetProcess3DEngine(
        L"\\GPU Engine(pid_4242_luid_0x00000000_0x00012345_phys_0_eng_0_engtype_3D)\\Utilization Percentage",
        4242), "matches target 3D engine");
    check(isTargetProcess3DEngine(
        L"\\GPU Engine(PID_4242_LUID_0x1_phys_0_eng_2_ENGTYPE_3d)\\Utilization Percentage",
        4242), "case-insensitive match");
    check(!isTargetProcess3DEngine(
        L"\\GPU Engine(pid_42420_luid_0x1_phys_0_eng_0_engtype_3D)\\Utilization Percentage",
        4242), "does not match PID prefix collision");
    check(!isTargetProcess3DEngine(
        L"\\GPU Engine(pid_4242_luid_0x1_phys_0_eng_0_engtype_Copy)\\Utilization Percentage",
        4242), "does not match copy engine");
    check(!isTargetProcess3DEngine(
        L"\\GPU Engine(pid_9000_luid_0x1_phys_0_eng_0_engtype_3D)\\Utilization Percentage",
        4242), "does not match another process");
    std::cout << "GpuEnginePolicyTests PASS\n";
}
