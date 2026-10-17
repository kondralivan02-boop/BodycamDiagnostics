#include "AutoTuneWindows.h"
#include "AutoTunePlan.h"
#include "ProcessFinder.h"
#include <Windows.h>
#include <ShlObj.h>
#include <dxgi.h>
#include <exception>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {
void message(HWND owner, const std::wstring& text, UINT flags = MB_ICONWARNING) {
    MessageBoxW(owner, text.c_str(), L"Bodycam AutoTune (экспериментально)", flags | MB_OK);
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"Unknown";
    std::wstring result(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}

// Checks each existing component, including the final file, to refuse
// symlinks/junctions instead of following redirected game/storage folders.
bool noReparse(const std::filesystem::path& path) {
    std::filesystem::path current;
    for (const auto& part : path) {
        current /= part;
        if (current == path.root_name()) continue;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return false;
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    }
    return true;
}

bool readSmall(const std::filesystem::path& file, std::string& bytes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) ||
        std::filesystem::file_size(file, ec) > 1024 * 1024 || ec) return false;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.eof() || !stream.bad();
}

std::filesystem::path configFile() {
    PWSTR local = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    if (FAILED(hr) || !local) { if (local) CoTaskMemFree(local); return {}; }
    const std::filesystem::path root = std::filesystem::path(local) / L"Bodycam" / L"Saved" / L"Config";
    CoTaskMemFree(local);
    std::filesystem::path found;
    for (const wchar_t* folder : {L"Window", L"Windows", L"WindowsNoEditor"}) {
        const auto candidate = root / folder / L"GameUserSettings.ini";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        if (!found.empty()) return {}; // Never guess between multiple files.
        found = candidate;
    }
    return found;
}

autotune::Hardware detectHardware() {
    autotune::Hardware h;
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) h.ramMiB = mem.ullTotalPhys / (1024 * 1024);
    h.width = static_cast<unsigned>((std::max)(0, GetSystemMetrics(SM_CXSCREEN)));
    h.height = static_cast<unsigned>((std::max)(0, GetSystemMetrics(SM_CYSCREEN)));
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT hr = factory->EnumAdapters1(i, &adapter);
            if (FAILED(hr)) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                h.vramMiB = (std::max)(h.vramMiB,
                    static_cast<std::uint64_t>(desc.DedicatedVideoMemory / (1024 * 1024)));
            }
            adapter->Release();
        }
        factory->Release();
    }
    return h;
}

// CREATE_NEW prevents clobbering an unrelated temporary file. Original data
// stays intact unless the durable temp write succeeds and the source is still
// byte-for-byte what was previewed.
bool atomicReplace(const std::filesystem::path& target,
                   const std::string& expected, const std::string& replacement,
                   std::wstring& error) {
    std::string now;
    if (!readSmall(target, now) || now != expected || !noReparse(target)) {
        error = L"Конфигурация изменилась после предварительного просмотра. Повторите попытку.";
        return false;
    }
    const auto temp = std::filesystem::path(target.wstring() +
        L".autotune-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = L"Нет доступа к созданию временного файла настроек (Windows " +
            std::to_wstring(GetLastError()) + L")";
        return false;
    }
    DWORD written = 0;
    const bool okay = WriteFile(h, replacement.data(),
        static_cast<DWORD>(replacement.size()), &written, nullptr) != FALSE &&
        written == replacement.size() && FlushFileBuffers(h) != FALSE;
    CloseHandle(h);
    if (!okay) {
        DeleteFileW(temp.c_str());
        error = L"Не удалось надёжно сохранить временный INI";
        return false;
    }
    if (!readSmall(target, now) || now != expected || !noReparse(target) ||
        !MoveFileExW(temp.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temp.c_str());
        error = L"Файл изменён или Windows запретила замену (код " +
            std::to_wstring(code) + L")";
        return false;
    }
    return true;
}
} // namespace

void runGameAutoTune(HWND owner, const std::filesystem::path& dataRoot,
                     bool restore, bool gizmoMode, bool diagnosticsRunning) {
    try {
    if (gizmoMode || diagnosticsRunning ||
        findProcessByName(L"Bodycam-Win64-Shipping.exe").pid ||
        findProcessByName(L"Bodycam.exe").pid) {
        message(owner, L"Изменение игровых настроек запрещено в режиме Gizmo, во время теста или при работающей игре. Закройте игру и отключите режим совместимости.");
        return;
    }
    if (dataRoot.empty() || !noReparse(dataRoot)) {
        message(owner, L"Каталог резервных копий недоступен или перенаправлен.");
        return;
    }
    const auto file = configFile();
    if (file.empty() || !noReparse(file)) {
        message(owner, L"Не найден единственный доступный GameUserSettings.ini. Поддерживаются Window, Windows и WindowsNoEditor. Если файлов несколько, выбор намеренно блокируется. Права доступа не обходятся.");
        return;
    }
    std::string old;
    if (!readSmall(file, old)) {
        message(owner, L"Нет разрешённого доступа к игровым настройкам или INI слишком велик.");
        return;
    }
    const auto backupDir = dataRoot / L"AutoTuneBackups";
    const auto backup = backupDir / (file.parent_path().filename().wstring() +
        L"-GameUserSettings.original.ini");
    std::wstring error;
    if (restore) {
        if (!noReparse(backup)) {
            message(owner, L"Исходная резервная копия отсутствует или недоступна.");
            return;
        }
        std::string original;
        if (!readSmall(backup, original)) {
            message(owner, L"Исходная резервная копия повреждена или недоступна.");
            return;
        }
        if (original == old) {
            message(owner, L"Исходные настройки уже восстановлены.", MB_ICONINFORMATION);
            return;
        }
        if (MessageBoxW(owner,
              L"Восстановить первоначальный GameUserSettings.ini? Все последующие изменения графики в этом файле будут заменены сохранённой копией.",
              L"Bodycam AutoTune — восстановление", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        if (!atomicReplace(file, old, original, error)) message(owner, error);
        else message(owner, L"Первоначальный GameUserSettings.ini восстановлен.", MB_ICONINFORMATION);
        return;
    }
    const auto hardware = detectHardware();
    autotune::Preset preset{};
    if (!autotune::suggest(hardware, preset)) {
        message(owner, L"Не удалось безопасно определить VRAM, RAM или разрешение. Автопрофиль не выбран.");
        return;
    }
    const auto plan = autotune::prepare(old, preset);
    if (!plan.supported) {
        message(owner, L"Автоприменение запрещено: формат GameUserSettings.ini не подтверждён для этой версии Bodycam. " + widen(plan.reason));
        return;
    }
    if (plan.changes.empty()) {
        message(owner, L"Поддерживаемые параметры уже соответствуют рекомендованному начальному профилю.", MB_ICONINFORMATION);
        return;
    }
    std::wstring preview =
        L"Начальный профиль (эвристика по железу): " + widen(autotune::name(preset)) +
        L"\nVRAM: " + std::to_wstring(hardware.vramMiB) + L" MiB, RAM: " +
        std::to_wstring(hardware.ramMiB) + L" MiB\n\n";
    for (const auto& change : plan.changes) {
        preview += widen(change.key) + L": " + std::to_wstring(change.before) +
            L" -> " + std::to_wstring(change.after) + L"\n";
    }
    preview += L"\nЭто не результат бенчмарка! DLSS и Frame Generation не изменяются. "
        L"Будет создана резервная копия. Применить настройки?";
    if (MessageBoxW(owner, preview.c_str(), L"Bodycam AutoTune — просмотр изменений",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::error_code ec;
    std::filesystem::create_directories(backupDir, ec);
    if (ec || !noReparse(backupDir)) {
        message(owner, L"Не удалось подготовить безопасный каталог резервных копий.");
        return;
    }
    std::string beforeBackup;
    if (!readSmall(file, beforeBackup) || beforeBackup != old) {
        message(owner, L"Файл игры изменился до резервного копирования. Настройки не изменены.");
        return;
    }
    if (!std::filesystem::exists(backup, ec)) {
        if (!CopyFileW(file.c_str(), backup.c_str(), TRUE)) {
            message(owner, L"Резервная копия не создана (Windows " +
                std::to_wstring(GetLastError()) + L"). Настройки не изменены.");
            return;
        }
    }
    std::string original;
    if (!noReparse(backup) || !readSmall(backup, original) || original.empty()) {
        message(owner, L"Проверка резервной копии не пройдена. Настройки не изменены.");
        return;
    }
    if (!atomicReplace(file, old, plan.updated, error)) {
        message(owner, error);
        return;
    }
    message(owner, L"Начальный профиль применён. Проверьте игру и результаты F9. Восстановление доступно в меню НАСТРОЙКИ.", MB_ICONINFORMATION);
    } catch (const std::exception&) {
        message(owner, L"Ошибка доступа к настройкам. Автонастройка остановлена.");
    }
}
