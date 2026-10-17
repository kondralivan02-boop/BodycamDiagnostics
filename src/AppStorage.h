#pragma once
#include <filesystem>
#include <string>
std::filesystem::path userDataDirectory(std::wstring* error);
std::filesystem::path writableSessionRoot(const std::filesystem::path& appRoot, bool perUserOnly, std::wstring* error);
