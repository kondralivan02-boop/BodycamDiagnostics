#include <filesystem>
#include <fstream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;

    {
        std::ofstream argsLog(
            L"mux_fixture_args.txt",
            std::ios::binary | std::ios::trunc);
        if (!argsLog)
            return 5;

        for (int i = 1; i < argc; ++i) {
            if (i > 1)
                argsLog << '\n';

            const std::wstring value = argv[i];
            for (const wchar_t c : value) {
                argsLog << (
                    c >= 0 && c <= 0x7f
                        ? static_cast<char>(c)
                        : '?');
            }
        }
    }

    const std::filesystem::path output = argv[argc - 1];
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return 3;

    out << "fake mux output";
    return out ? 0 : 4;
}
