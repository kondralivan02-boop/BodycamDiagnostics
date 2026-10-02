#include <fstream>
#include <filesystem>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;

    const std::filesystem::path output = argv[argc - 1];
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return 3;

    out << "fake mux output";
    return out ? 0 : 4;
}
