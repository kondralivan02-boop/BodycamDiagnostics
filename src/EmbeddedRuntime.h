#pragma once
#include <filesystem>
#include <string>

// Only writes resources beneath the supplied user runtime directory. Existing
// mismatching files are refused, never executed or silently replaced.
bool unpackEmbeddedPayload(const std::filesystem::path& root, std::wstring* error);
bool prepareEmbeddedRuntime(std::filesystem::path& runtimeRoot, std::wstring* error);
