#include <Windows.h>

#include <filesystem>
#include <fstream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;

    const std::filesystem::path outputPattern = argv[argc - 1];
    const auto marker =
        outputPattern.parent_path() / L"recorder_fixture.marker";

    std::error_code ec;
    if (std::filesystem::is_regular_file(marker, ec))
        return 24;

    {
        std::ofstream out(marker, std::ios::binary | std::ios::trunc);
        if (!out)
            return 3;
        out << "first-run";
    }

    Sleep(900);
    return 23;
}
