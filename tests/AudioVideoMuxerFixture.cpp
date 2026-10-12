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

    bool includesAudio = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"1:a:0") {
            includesAudio = true;
            break;
        }
    }

    // The fixture deliberately fails audio muxing to exercise recovery.
    if (includesAudio && std::filesystem::exists(L"mux_fixture_fail_audio.txt")) {
        std::ofstream(L"mux_fixture_audio_attempted.txt") << "audio mux attempted";
        return 17;
    }
    if (std::filesystem::exists(L"mux_fixture_fail_all.txt"))
        return 18;

    const std::filesystem::path output = argv[argc - 1];
    if (includesAudio && std::filesystem::exists(L"mux_fixture_empty_audio.txt")) {
        // Simulate an FFmpeg success code with an invalid zero-byte MKV.
        std::ofstream empty(output, std::ios::binary | std::ios::trunc);
        return empty ? 0 : 4;
    }
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return 3;

    out << "fake mux output";
    return out ? 0 : 4;
}
