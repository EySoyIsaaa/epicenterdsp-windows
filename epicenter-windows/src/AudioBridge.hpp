#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class AudioBridge {
public:
    AudioBridge();
    ~AudioBridge();

    AudioBridge(const AudioBridge&) = delete;
    AudioBridge& operator=(const AudioBridge&) = delete;

    bool probe(std::wstring& detail);
    bool start(float intensity, std::wstring& error);
    void stop();
    void setIntensity(float intensity);

    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    bool isHealthy() const { return healthy_.load(std::memory_order_acquire); }
    std::wstring lastError() const;
    std::wstring outputName() const;

    static bool recoverStaleRouting();
    static int runWatchdog(unsigned long parentProcessId);

private:
    void audioThreadMain(std::wstring captureId, std::wstring renderId);
    void setThreadError(const std::wstring& message);

    std::atomic<float> intensity_ { 60.0f };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> running_ { false };
    std::atomic<bool> healthy_ { false };

    void* stopEvent_ = nullptr;
    void* readyEvent_ = nullptr;
    std::thread audioThread_;

    mutable std::mutex stateMutex_;
    std::wstring lastError_;
    std::wstring outputName_;
    std::wstring originalConsoleId_;
    std::wstring originalMultimediaId_;
    std::wstring originalCommunicationsId_;
};
