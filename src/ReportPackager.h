#pragma once
#include <filesystem>
#include <string>

bool packageSessionReport(const std::filesystem::path& sessionDirectory,
                          const std::filesystem::path& outputZip,
                          std::wstring* error = nullptr);
