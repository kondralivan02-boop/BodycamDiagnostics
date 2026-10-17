#include "AppStorage.h"

#include <Windows.h>
#include <shlobj.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kDataRootVariable[] = L"BODYCAM_DIAG_DATA_ROOT";

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

bool fail(std::wstring* error, const std::wstring& detail) {
    if (error) *error = detail;
    return false;
}

// Do not follow junctions or symlinks when selecting or creating a configured
// data directory. In particular, a Sessions link must not redirect recordings
// into system, game or Gizmo directories.
bool safeDirectoryChain(
    const std::filesystem::path& directory,
    bool createMissing,
    std::wstring* error) {

    std::filesystem::path part;
    for (const auto& component : directory) {
        part /= component;
        if (part == directory.root_name()) continue;

        DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD lookupError = GetLastError();
            if (lookupError != ERROR_FILE_NOT_FOUND &&
                lookupError != ERROR_PATH_NOT_FOUND) {
                return fail(error, L"Нет доступа к каталогу данных: " + part.wstring() +
                    L" (код " + std::to_wstring(lookupError) + L")");
            }
            if (!createMissing) continue;
            if (!CreateDirectoryW(part.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                return fail(error, L"Не удалось создать каталог данных: " + part.wstring() +
                    L" (код " + std::to_wstring(GetLastError()) + L")");
            }
            attributes = GetFileAttributesW(part.c_str());
        }
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return fail(error, L"Небезопасный каталог данных (файл или перенаправление): " +
                part.wstring());
        }
    }
    return true;
}

struct DataRootChoice {
    std::filesystem::path path;
    bool configured = false;
};

// A specified but invalid override MUST NOT silently fall back to LocalAppData.
DataRootChoice selectDataRoot(std::wstring* error) {
    if (error) error->clear();

    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(kDataRootVariable, nullptr, 0);
    const DWORD envError = GetLastError();
    if (required != 0 || envError != ERROR_ENVVAR_NOT_FOUND) {
        if (required == 0 || required > 32767) {
            fail(error, L"BODYCAM_DIAG_DATA_ROOT задан пустым или слишком длинным");
            return {};
        }
        std::vector<wchar_t> buffer(required);
        const DWORD length =
            GetEnvironmentVariableW(kDataRootVariable, buffer.data(), required);
        if (length == 0 || length >= required) {
            fail(error, L"Не удалось прочитать BODYCAM_DIAG_DATA_ROOT");
            return {};
        }
        const std::filesystem::path root(
            std::wstring(buffer.data(), static_cast<std::size_t>(length)));
        const std::wstring drive = root.root_name().wstring();
        if (!root.is_absolute() ||
            drive.size() != 2 ||
            !((drive[0] >= L'A' && drive[0] <= L'Z') ||
              (drive[0] >= L'a' && drive[0] <= L'z')) ||
            drive[1] != L':' ||
            root == root.root_path()) {
            fail(error, L"BODYCAM_DIAG_DATA_ROOT должен быть абсолютным каталогом "
                L"на локальном диске, например D:\\BodycamDiagnosticsData");
            return {};
        }
        for (const auto& component : root) {
            if (component == root.root_name() ||
                component == root.root_directory()) continue;
            const std::wstring name = component.wstring();
            if (name.empty() || name == L"." || name == L".." ||
                name.find_first_of(L"<>:\"|?*") != std::wstring::npos ||
                name.back() == L'.' || name.back() == L' ') {
                fail(error, L"Небезопасный компонент BODYCAM_DIAG_DATA_ROOT: " + name);
                return {};
            }
        }
        const UINT driveType = GetDriveTypeW(root.root_path().c_str());
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            fail(error, L"BODYCAM_DIAG_DATA_ROOT должен находиться на доступном "
                L"локальном диске: " + root.root_path().wstring());
            return {};
        }
        if (!safeDirectoryChain(root, false, error)) return {};
        return {root, true};
    }

    PWSTR local = nullptr;
    const HRESULT result =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local);
    if (FAILED(result) || !local) {
        fail(error, L"Не удалось получить личный каталог LocalAppData (код " +
            std::to_wstring(static_cast<unsigned long>(result)) + L")");
        if (local) CoTaskMemFree(local);
        return {};
    }
    const auto root = std::filesystem::path(local) / L"BodycamDiagnostics";
    CoTaskMemFree(local);
    return {root, false};
}
} // namespace

std::filesystem::path userDataDirectory(std::wstring* error) {
    return selectDataRoot(error).path;
}

std::filesystem::path writableSessionRoot(
    const std::filesystem::path& appRoot,
    bool perUserOnly,
    std::wstring* error) {

    if (!perUserOnly) {
        const auto sessions = appRoot / L"Sessions";
        std::error_code ec;
        std::filesystem::create_directories(sessions, ec);
        const auto probe =
            sessions / (L".bodycam-write-check-" + std::to_wstring(GetCurrentProcessId()));
        Handle file;
        file.value = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (!ec && file.value != INVALID_HANDLE_VALUE) return sessions;
    }

    const DataRootChoice root = selectDataRoot(error);
    if (root.path.empty()) return {};
    const auto sessions = root.path / L"Sessions";
    if (root.configured && !safeDirectoryChain(sessions, true, error))
        return {};
    return sessions;
}
