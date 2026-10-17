#include "EmbeddedRuntime.h"
#include "AppStorage.h"
#include "ToolPreflight.h"
#include "EmbeddedPayload.h"
#include <Windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdint>

namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

bool fail(std::wstring* error, const std::wstring& detail) {
    if (error) *error = detail;
    return false;
}

bool safeDirectory(const std::filesystem::path& root, std::wstring* error) {
    // Refuse junctions/symlinks in the runtime path, including ancestors. Do
    // not follow redirection to a game, system folder or another user's data.
    std::filesystem::path part;
    for (const auto& component : root) {
        part /= component;
        if (part == root.root_name()) continue;
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(attributes & FILE_ATTRIBUTE_DIRECTORY)))
            return fail(error, L"Небезопасный каталог runtime: " + part.wstring());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                return fail(error, L"Не удалось создать каталог runtime: " + part.wstring() +
                    L" (код " + std::to_wstring(GetLastError()) + L")");
            const DWORD created = GetFileAttributesW(part.c_str());
            if (created == INVALID_FILE_ATTRIBUTES || (created & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !(created & FILE_ATTRIBUTE_DIRECTORY))
                return fail(error, L"Каталог runtime недоступен или перенаправлен: " + part.wstring());
        }
    }
    return true;
}
}

bool unpackEmbeddedPayload(const std::filesystem::path& root, std::wstring* error) {
    if (error) error->clear();
    if (!root.is_absolute())
        return fail(error, L"Каталог runtime должен быть абсолютным: " + root.wstring());
    if (!safeDirectory(root, error)) return false;
    // Serialize publication across app instances; no administrator privileges,
    // elevation, runtime downloads or global named objects are required.
    Handle lock;
    lock.value = CreateFileW((root / L".unpack.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock.value == INVALID_HANDLE_VALUE)
        return fail(error, L"Runtime занят другой копией приложения или недоступен: " + root.wstring() +
            L" (код " + std::to_wstring(GetLastError()) + L")");
    BY_HANDLE_FILE_INFORMATION lockInfo{};
    if (!GetFileInformationByHandle(lock.value, &lockInfo) ||
        (lockInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(error, L"Небезопасный файл блокировки runtime");
    for (const auto& asset : kEmbeddedAssets) {
        const auto target = root / asset.name;
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                fileSha256Hex(target) != asset.sha)
                return fail(error, L"Проверка SHA-256 не пройдена: " + target.wstring() +
                    L". Инструмент не будет запущен. Сохраните данные и удалите повреждённый runtime-каталог перед повторным запуском.");
            continue;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            return fail(error, L"Нет доступа к runtime-файлу: " + target.wstring());
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(asset.id), RT_RCDATA);
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const auto* bytes = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
        const DWORD length = resource ? SizeofResource(nullptr, resource) : 0;
        if (!bytes || !length) return fail(error, L"Встроенный ресурс отсутствует: " + std::wstring(asset.name));
        const auto temporary = root / (std::wstring(asset.name) + L"." +
            std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp");
        bool written = false;
        {
            Handle file;
            file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.value == INVALID_HANDLE_VALUE)
                return fail(error, L"Не удалось создать временный runtime-файл: " + temporary.wstring());
            DWORD offset = 0;
            while (offset < length) {
                DWORD count = 0;
                const DWORD chunk = (std::min)(length - offset, DWORD{1024 * 1024});
                if (!WriteFile(file.value, bytes + offset, chunk, &count, nullptr) || !count) break;
                offset += count;
            }
            written = offset == length && FlushFileBuffers(file.value);
        }
        if (!written || fileSha256Hex(temporary) != asset.sha) {
            DeleteFileW(temporary.c_str());
            return fail(error, L"Не удалось записать и проверить встроенный ресурс: " + target.wstring());
        }
        // No REPLACE_EXISTING: never overwrite a file another actor published.
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return fail(error, L"Не удалось сохранить runtime-файл: " + target.wstring());
        }
        if (fileSha256Hex(target) != asset.sha)
            return fail(error, L"Файл runtime изменён после распаковки: " + target.wstring());
    }
    return runtimeHashManifestTrusted(root / L"runtime-tools.sha256") ||
        fail(error, L"Встроенный SHA-256 манифест не совпадает с доверенным якорем");
}

bool prepareEmbeddedRuntime(std::filesystem::path& runtimeRoot, std::wstring* error) {
    const auto user = userDataDirectory(error);
    if (user.empty()) return false;
    runtimeRoot = user / L"Runtime" / kEmbeddedVersion;
    return unpackEmbeddedPayload(runtimeRoot, error);
}

