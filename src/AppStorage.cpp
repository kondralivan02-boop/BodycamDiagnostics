#include "AppStorage.h"
#include <Windows.h>
#include <shlobj.h>
namespace {
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
bool fail(std::wstring* error, const std::wstring& detail) { if(error) *error = detail; return false; }
}

std::filesystem::path userDataDirectory(std::wstring* error) {
    PWSTR local = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local);
    if (FAILED(result)) {
        fail(error, L"Не удалось получить личный каталог LocalAppData (код " +
            std::to_wstring(static_cast<unsigned long>(result)) + L")");
        return {};
    }
    const auto root = std::filesystem::path(local) / L"BodycamDiagnostics";
    CoTaskMemFree(local);
    return root;
}

std::filesystem::path writableSessionRoot(const std::filesystem::path& appRoot, bool perUserOnly, std::wstring* error) {
    if (!perUserOnly) {
        const auto sessions = appRoot / L"Sessions";
        std::error_code ec;
        std::filesystem::create_directories(sessions, ec);
        const auto probe = sessions / (L".bodycam-write-check-" + std::to_wstring(GetCurrentProcessId()));
        Handle file;
        file.value = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (!ec && file.value != INVALID_HANDLE_VALUE) return sessions;
    }
    const auto user = userDataDirectory(error);
    return user.empty() ? std::filesystem::path{} : user / L"Sessions";
}
