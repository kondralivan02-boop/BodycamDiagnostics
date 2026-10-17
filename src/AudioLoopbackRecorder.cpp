#include "AudioLoopbackRecorder.h"

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <vector>

namespace {
template <typename T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool writeBytes(std::ofstream& out, const void* data, std::size_t bytes) {
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    return static_cast<bool>(out);
}

bool writeU32(std::ofstream& out, std::uint32_t value) {
    return writeBytes(out, &value, sizeof(value));
}

bool writeWaveHeader(std::ofstream& out,
                     const WAVEFORMATEX* format,
                     std::uint32_t dataBytes,
                     std::streampos* dataSizeOffset) {
    if (!format) return false;

    const bool basicFormat =
        format->wFormatTag == WAVE_FORMAT_PCM ||
        format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

    const std::uint32_t fmtSize = basicFormat
        ? 16u
        : static_cast<std::uint32_t>(18u + format->cbSize);

    const std::uint32_t riffSize =
        4u + (8u + fmtSize) + 8u + dataBytes;

    out.seekp(0, std::ios::beg);
    if (!writeBytes(out, "RIFF", 4)) return false;
    if (!writeU32(out, riffSize)) return false;
    if (!writeBytes(out, "WAVE", 4)) return false;

    if (!writeBytes(out, "fmt ", 4)) return false;
    if (!writeU32(out, fmtSize)) return false;
    if (!writeBytes(out, format, fmtSize)) return false;

    if (!writeBytes(out, "data", 4)) return false;
    if (dataSizeOffset) *dataSizeOffset = out.tellp();
    if (!writeU32(out, dataBytes)) return false;

    return true;
}

bool updateWaveSizes(std::ofstream& out,
                     std::streampos dataSizeOffset,
                     std::uint32_t dataBytes) {
    const auto end = out.tellp();
    if (end < std::streampos(8)) return false;

    const auto totalBytes = static_cast<std::uint64_t>(end);
    if (totalBytes > 0xFFFFFFFFull) return false;

    const std::uint32_t riffSize = static_cast<std::uint32_t>(totalBytes - 8u);

    out.seekp(4, std::ios::beg);
    if (!writeU32(out, riffSize)) return false;

    out.seekp(dataSizeOffset, std::ios::beg);
    if (!writeU32(out, dataBytes)) return false;

    out.seekp(end);
    out.flush();
    return static_cast<bool>(out);
}
}

AudioLoopbackRecorder::AudioLoopbackRecorder() = default;

AudioLoopbackRecorder::~AudioLoopbackRecorder() {
    stop();
}

bool AudioLoopbackRecorder::start(const std::filesystem::path& wavOutput,
                                  std::wstring* error) {
    if (running()) return true;

    {
        std::lock_guard lock(readyMutex_);
        ready_ = false;
        readyOk_ = false;
        readyError_.clear();
        runtimeError_.clear();
    }

    wavOutput_ = wavOutput;
    capturedDataBytes_.store(0);
    runtimeHealthy_.store(true);
    stopRequested_.store(false);
    running_.store(true);

    try {
        thread_ = std::thread(&AudioLoopbackRecorder::worker, this);
    } catch (...) {
        running_.store(false);
        if (error) *error = L"Cannot start WASAPI audio thread";
        return false;
    }

    std::unique_lock lock(readyMutex_);
    const bool signaled = readyCv_.wait_for(
        lock, std::chrono::seconds(5), [this] { return ready_; });

    if (!signaled) {
        lock.unlock();
        running_.store(false);
        if (thread_.joinable()) thread_.join();
        if (error) *error = L"WASAPI initialization timed out";
        return false;
    }

    if (!readyOk_) {
        const auto message = readyError_;
        lock.unlock();
        stopRequested_.store(true);
        running_.store(false);
        if (thread_.joinable()) thread_.join();
        if (error) *error = message;
        return false;
    }

    lock.unlock();

    std::wstring runtimeError;
    if (!healthy(&runtimeError)) {
        stopRequested_.store(true);
        running_.store(false);
        if (thread_.joinable())
            thread_.join();
        if (error)
            *error = runtimeError;
        return false;
    }

    return true;
}

bool AudioLoopbackRecorder::stop(std::wstring* error) {
    stopRequested_.store(true);
    running_.store(false);

    if (thread_.joinable())
        thread_.join();

    if (!runtimeHealthy_.load()) {
        std::lock_guard lock(readyMutex_);
        if (error) {
            *error =
                runtimeError_.empty()
                    ? L"WASAPI capture failed during recording"
                    : runtimeError_;
        }
        return false;
    }

    // A header-only WAV has no captured audio data. Never pass it to FFmpeg
    // as a successful audio track; the video can still be saved without it.
    if (capturedDataBytes_.load() == 0) {
        if (error)
            *error = L"WASAPI captured no audio frames";
        return false;
    }

    return true;
}

bool AudioLoopbackRecorder::healthy(std::wstring* error) {
    if (!runtimeHealthy_.load()) {
        std::lock_guard lock(readyMutex_);
        if (error) {
            *error =
                runtimeError_.empty()
                    ? L"WASAPI capture failed during recording"
                    : runtimeError_;
        }
        return false;
    }

    if (!stopRequested_.load() &&
        !running_.load()) {
        if (error)
            *error = L"WASAPI capture stopped unexpectedly";
        return false;
    }

    return true;
}

void AudioLoopbackRecorder::recordRuntimeFailure(
    std::wstring error) {

    runtimeHealthy_.store(false);
    std::lock_guard lock(readyMutex_);
    if (runtimeError_.empty())
        runtimeError_ = std::move(error);
}

void AudioLoopbackRecorder::signalReady(bool ok, std::wstring error) {
    {
        std::lock_guard lock(readyMutex_);
        ready_ = true;
        readyOk_ = ok;
        readyError_ = std::move(error);
    }
    readyCv_.notify_all();
}

void AudioLoopbackRecorder::worker() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coInitialized = SUCCEEDED(coHr);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* mixFormat = nullptr;

    auto fail = [&](const wchar_t* stage, HRESULT hr) {
        signalReady(false,
            std::wstring(stage) + L" failed, HRESULT=0x" +
            [] (HRESULT value) {
                wchar_t buffer[16]{};
                swprintf_s(buffer, L"%08X", static_cast<unsigned int>(value));
                return std::wstring(buffer);
            }(hr));
    };

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        fail(L"CoCreateInstance(MMDeviceEnumerator)", hr);
        goto cleanup;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        fail(L"GetDefaultAudioEndpoint", hr);
        goto cleanup;
    }

    hr = device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&audioClient));
    if (FAILED(hr)) {
        fail(L"IMMDevice::Activate(IAudioClient)", hr);
        goto cleanup;
    }

    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || !mixFormat) {
        fail(L"IAudioClient::GetMixFormat", hr);
        goto cleanup;
    }

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        0,
        0,
        mixFormat,
        nullptr);
    if (FAILED(hr)) {
        fail(L"IAudioClient::Initialize(loopback)", hr);
        goto cleanup;
    }

    hr = audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&captureClient));
    if (FAILED(hr)) {
        fail(L"IAudioClient::GetService(IAudioCaptureClient)", hr);
        goto cleanup;
    }

    {
        std::error_code ec;
        std::filesystem::create_directories(wavOutput_.parent_path(), ec);
        if (ec) {
            signalReady(false, L"Cannot create audio output directory");
            goto cleanup;
        }
    }

    {
        std::ofstream out(wavOutput_, std::ios::binary | std::ios::trunc);
        if (!out) {
            signalReady(false, L"Cannot create game_audio.wav");
            goto cleanup;
        }

        std::streampos dataSizeOffset{};
        if (!writeWaveHeader(out, mixFormat, 0, &dataSizeOffset)) {
            signalReady(false, L"Cannot write WAV header");
            goto cleanup;
        }

        hr = audioClient->Start();
        if (FAILED(hr)) {
            fail(L"IAudioClient::Start", hr);
            goto cleanup;
        }

        signalReady(true);

        std::uint64_t totalDataBytes = 0;
        std::vector<std::uint8_t> silence;
        auto nextHeaderRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        while (running_.load()) {
            UINT32 packetFrames = 0;
            hr = captureClient->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                recordRuntimeFailure(
                    L"IAudioCaptureClient::GetNextPacketSize failed");
                break;
            }

            while (packetFrames > 0 && running_.load()) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(
                    &data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    recordRuntimeFailure(
                        L"IAudioCaptureClient::GetBuffer failed");
                    break;
                }

                const std::size_t bytes =
                    static_cast<std::size_t>(frames) * mixFormat->nBlockAlign;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silence.assign(bytes, 0);
                    out.write(reinterpret_cast<const char*>(silence.data()),
                              static_cast<std::streamsize>(silence.size()));
                } else if (data && bytes > 0) {
                    out.write(reinterpret_cast<const char*>(data),
                              static_cast<std::streamsize>(bytes));
                }

                hr = captureClient->ReleaseBuffer(frames);
                if (FAILED(hr)) {
                    recordRuntimeFailure(
                        L"IAudioCaptureClient::ReleaseBuffer failed");
                    break;
                }

                totalDataBytes += bytes;
                capturedDataBytes_.store(totalDataBytes);
                if (totalDataBytes > 0xFFFFFFFFull) {
                    recordRuntimeFailure(
                        L"WASAPI WAV exceeded RIFF size limit");
                    running_.store(false);
                    break;
                }

                hr = captureClient->GetNextPacketSize(&packetFrames);
                if (FAILED(hr)) {
                    recordRuntimeFailure(
                        L"IAudioCaptureClient::GetNextPacketSize failed");
                    break;
                }
            }

            if (!out) {
                recordRuntimeFailure(
                    L"Cannot write game_audio.wav");
                break;
            }

            if (FAILED(hr))
                break;

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextHeaderRefresh) {
                if (!updateWaveSizes(
                        out,
                        dataSizeOffset,
                        static_cast<std::uint32_t>(
                            totalDataBytes))) {
                    recordRuntimeFailure(
                        L"Cannot refresh game_audio.wav header");
                    break;
                }
                nextHeaderRefresh =
                    now + std::chrono::seconds(1);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const HRESULT stopHr =
            audioClient->Stop();
        if (FAILED(stopHr) &&
            runtimeHealthy_.load()) {
            recordRuntimeFailure(
                L"IAudioClient::Stop failed");
        }

        if (!updateWaveSizes(
                out,
                dataSizeOffset,
                static_cast<std::uint32_t>(
                    (std::min<std::uint64_t>)(
                        totalDataBytes,
                        0xFFFFFFFFull))) &&
            runtimeHealthy_.load()) {
            recordRuntimeFailure(
                L"Cannot finalize game_audio.wav header");
        }

        out.close();
    }

cleanup:
    running_.store(false);

    if (mixFormat) CoTaskMemFree(mixFormat);
    safeRelease(captureClient);
    safeRelease(audioClient);
    safeRelease(device);
    safeRelease(enumerator);

    if (coInitialized)
        CoUninitialize();
}
