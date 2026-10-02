#include <Windows.h>

#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--terminate_existing_session") == 0)
            return 0;
    }

    Sleep(900);
    return 23;
}
