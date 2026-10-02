#include "BuildInfo.h"

bool writeBuildInfoReport(
    const std::filesystem::path&,
    std::wstring* error) {

    if (error)
        *error = L"Build provenance writer not implemented";
    return false;
}
