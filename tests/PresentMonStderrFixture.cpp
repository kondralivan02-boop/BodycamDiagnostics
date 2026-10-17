#include <Windows.h>
#include <cstring>
#include <string>

bool write(HANDLE output, const std::string& text) {
    DWORD written = 0;
    return WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
}
int main(int argc, char** argv) {
    bool delayed = false, flood = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--terminate_existing_session") == 0) return 0;
        if (std::strcmp(argv[i], "--process_id") == 0 && i + 1 < argc) {
            delayed = std::strcmp(argv[i+1], "6002") == 0;
            flood = std::strcmp(argv[i+1], "6003") == 0;
        }
    }
    if (!write(GetStdHandle(STD_OUTPUT_HANDLE), "Application,ProcessID,FrameTime\r\nfixture,6,16.0\r\n")) return 8;
    if (!write(GetStdHandle(STD_ERROR_HANDLE), "fixture diagnostic: ETW access denied; exit 6\n")) return 9;
    if (flood) {
        const std::string chunk(4096, 'x');
        for (int i = 0; i < 512; ++i) if (!write(GetStdHandle(STD_ERROR_HANDLE), chunk)) return 10;
    }
    if (delayed) Sleep(900);
    return 6;
}
