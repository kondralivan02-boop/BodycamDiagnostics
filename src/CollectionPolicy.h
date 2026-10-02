#pragma once

struct CollectionPolicy {
    bool windowsEvents = true;
    bool bodycamLogs = true;
    bool fullscreenHelper = true;
    bool presentMon = true;
    bool telemetry = true;

    static constexpr CollectionPolicy forGizmo(bool enabled) noexcept {
        return {!enabled, !enabled, !enabled, true, true};
    }
};
