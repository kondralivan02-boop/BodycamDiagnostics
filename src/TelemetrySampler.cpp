#include "TelemetrySampler.h"
#include <Psapi.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
using nvmlReturn_t = int;
using nvmlDevice_t = void*;
constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr unsigned int NVML_TEMPERATURE_GPU = 0;
constexpr unsigned int NVML_CLOCK_GRAPHICS = 0;
constexpr unsigned int NVML_CLOCK_MEM = 2;

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

class NvmlReader {
public:
    ~NvmlReader() { close(); }

    bool open() {
        if (module_) return true;
        module_ = LoadLibraryW(L"nvml.dll");
        if (!module_) return false;

        init_ = reinterpret_cast<InitFn>(GetProcAddress(module_, "nvmlInit_v2"));
        shutdown_ = reinterpret_cast<ShutdownFn>(GetProcAddress(module_, "nvmlShutdown"));
        getHandle_ = reinterpret_cast<GetHandleFn>(GetProcAddress(module_, "nvmlDeviceGetHandleByIndex_v2"));
        getUtil_ = reinterpret_cast<GetUtilFn>(GetProcAddress(module_, "nvmlDeviceGetUtilizationRates"));
        getMemory_ = reinterpret_cast<GetMemoryFn>(GetProcAddress(module_, "nvmlDeviceGetMemoryInfo"));
        getTemp_ = reinterpret_cast<GetTempFn>(GetProcAddress(module_, "nvmlDeviceGetTemperature"));
        getClock_ = reinterpret_cast<GetClockFn>(GetProcAddress(module_, "nvmlDeviceGetClockInfo"));

        if (!init_ || !shutdown_ || !getHandle_ || !getUtil_ || !getMemory_ || !getTemp_ || !getClock_) {
            close();
            return false;
        }
        if (init_() != NVML_SUCCESS) {
            close();
            return false;
        }
        initialized_ = true;
        if (getHandle_(0, &device_) != NVML_SUCCESS) {
            close();
            return false;
        }
        return true;
    }

    void close() {
        if (initialized_ && shutdown_) shutdown_();
        initialized_ = false;
        device_ = nullptr;
        if (module_) FreeLibrary(module_);
        module_ = nullptr;
        init_ = nullptr;
        shutdown_ = nullptr;
        getHandle_ = nullptr;
        getUtil_ = nullptr;
        getMemory_ = nullptr;
        getTemp_ = nullptr;
        getClock_ = nullptr;
    }

    bool sample(unsigned& gpuUtil, unsigned& memUtil, unsigned long long& vramUsedMiB,
                unsigned long long& vramTotalMiB, unsigned& tempC,
                unsigned& graphicsMHz, unsigned& memoryMHz) {
        if (!device_) return false;

        nvmlUtilization_t util{};
        nvmlMemory_t mem{};
        unsigned temp = 0;
        unsigned gfx = 0;
        unsigned memClock = 0;

        if (getUtil_(device_, &util) != NVML_SUCCESS) return false;
        if (getMemory_(device_, &mem) != NVML_SUCCESS) return false;
        if (getTemp_(device_, NVML_TEMPERATURE_GPU, &temp) != NVML_SUCCESS) return false;
        if (getClock_(device_, NVML_CLOCK_GRAPHICS, &gfx) != NVML_SUCCESS) gfx = 0;
        if (getClock_(device_, NVML_CLOCK_MEM, &memClock) != NVML_SUCCESS) memClock = 0;

        gpuUtil = util.gpu;
        memUtil = util.memory;
        vramUsedMiB = mem.used / (1024ull * 1024ull);
        vramTotalMiB = mem.total / (1024ull * 1024ull);
        tempC = temp;
        graphicsMHz = gfx;
        memoryMHz = memClock;
        return true;
    }

private:
    using InitFn = nvmlReturn_t (*)();
    using ShutdownFn = nvmlReturn_t (*)();
    using GetHandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using GetUtilFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
    using GetMemoryFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
    using GetTempFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
    using GetClockFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);

    HMODULE module_ = nullptr;
    bool initialized_ = false;
    nvmlDevice_t device_ = nullptr;
    InitFn init_ = nullptr;
    ShutdownFn shutdown_ = nullptr;
    GetHandleFn getHandle_ = nullptr;
    GetUtilFn getUtil_ = nullptr;
    GetMemoryFn getMemory_ = nullptr;
    GetTempFn getTemp_ = nullptr;
    GetClockFn getClock_ = nullptr;
};

unsigned long long ftToU64(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

std::string isoTimestampLocal() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char b[64]{};
    sprintf_s(b, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}
}

TelemetrySampler::TelemetrySampler() = default;

TelemetrySampler::~TelemetrySampler() {
    stop();
}

bool TelemetrySampler::start(DWORD targetPid, const std::filesystem::path& csvOutput, std::wstring* error) {
    if (running()) return true;

    process_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, targetPid);
    if (!process_) {
        if (error) *error = L"OpenProcess failed: " + std::to_wstring(GetLastError());
        return false;
    }

    targetPid_ = targetPid;
    csvOutput_ = csvOutput;

    std::ofstream probe(csvOutput_, std::ios::out | std::ios::trunc);
    if (!probe) {
        if (error) *error = L"Cannot create telemetry.csv";
        CloseHandle(process_);
        process_ = nullptr;
        return false;
    }
    probe << "timestamp,pid,process_cpu_pct,working_set_mib,gpu_util_pct,gpu_mem_util_pct,vram_used_mib,vram_total_mib,gpu_temp_c,gpu_graphics_mhz,gpu_memory_mhz\n";
    probe.flush();
    probe.close();

    running_.store(true);
    try {
        thread_ = std::thread(&TelemetrySampler::worker, this);
    } catch (...) {
        running_.store(false);
        CloseHandle(process_);
        process_ = nullptr;
        if (error) *error = L"Cannot start telemetry thread";
        return false;
    }
    return true;
}

void TelemetrySampler::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}

void TelemetrySampler::worker() {
    std::ofstream out(csvOutput_, std::ios::out | std::ios::app);
    if (!out) {
        running_.store(false);
        return;
    }

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    const double logicalProcessors = si.dwNumberOfProcessors ? static_cast<double>(si.dwNumberOfProcessors) : 1.0;

    FILETIME pCreate{}, pExit{}, pKernel{}, pUser{};
    GetProcessTimes(process_, &pCreate, &pExit, &pKernel, &pUser);
    unsigned long long prevProc = ftToU64(pKernel) + ftToU64(pUser);

    FILETIME wall{};
    GetSystemTimeAsFileTime(&wall);
    unsigned long long prevWall = ftToU64(wall);

    NvmlReader nvml;
    const bool nvmlReady = nvml.open();

    while (running_.load()) {
        const auto wakeAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        double processCpu = 0.0;
        if (GetProcessTimes(process_, &pCreate, &pExit, &pKernel, &pUser)) {
            const unsigned long long procNow = ftToU64(pKernel) + ftToU64(pUser);
            GetSystemTimeAsFileTime(&wall);
            const unsigned long long wallNow = ftToU64(wall);
            const unsigned long long dProc = procNow - prevProc;
            const unsigned long long dWall = wallNow - prevWall;
            if (dWall > 0) {
                processCpu = (static_cast<double>(dProc) / static_cast<double>(dWall)) * 100.0 / logicalProcessors;
                if (processCpu < 0.0) processCpu = 0.0;
                if (processCpu > 100.0) processCpu = 100.0;
            }
            prevProc = procNow;
            prevWall = wallNow;
        }

        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        double workingSetMiB = 0.0;
        if (K32GetProcessMemoryInfo(process_, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            workingSetMiB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }

        unsigned gpuUtil = 0, gpuMemUtil = 0, tempC = 0, gfxMHz = 0, memMHz = 0;
        unsigned long long vramUsed = 0, vramTotal = 0;
        const bool gpuOk = nvmlReady && nvml.sample(gpuUtil, gpuMemUtil, vramUsed, vramTotal, tempC, gfxMHz, memMHz);

        out << isoTimestampLocal() << ',' << targetPid_ << ','
            << std::fixed << std::setprecision(2) << processCpu << ','
            << std::fixed << std::setprecision(1) << workingSetMiB << ',';

        if (gpuOk) {
            out << gpuUtil << ',' << gpuMemUtil << ',' << vramUsed << ',' << vramTotal << ','
                << tempC << ',' << gfxMHz << ',' << memMHz;
        } else {
            out << ",,,,,,";
        }
        out << '\n';
        out.flush();

        std::this_thread::sleep_until(wakeAt);
    }
}
