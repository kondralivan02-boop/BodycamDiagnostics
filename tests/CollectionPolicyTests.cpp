#include "CollectionPolicy.h"
#include <stdexcept>

void check(bool value) { if (!value) throw std::runtime_error("collection policy mismatch"); }
int main() {
    const auto normal = CollectionPolicy::forGizmo(false);
    check(normal.windowsEvents && normal.bodycamLogs && normal.fullscreenHelper);
    const auto gizmo = CollectionPolicy::forGizmo(true);
    check(!gizmo.windowsEvents && !gizmo.bodycamLogs && !gizmo.fullscreenHelper);
    check(gizmo.presentMon && gizmo.telemetry);
}
