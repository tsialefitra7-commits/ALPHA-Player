#pragma once
#ifdef _WIN32
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <windows.h>
#include "core/player_core.hpp"
namespace alpha::win32 {
class AudioEngine {
public:
    ~AudioEngine();
    bool play(const std::filesystem::path& file,HWND window,UINT finishedMessage);
    void pause(bool value) noexcept;
    void stop() noexcept;
    void setVolume(float value) noexcept;
    void setEqualizer(const std::array<float,10>& gains) noexcept;
    bool playing() const noexcept { return playing_.load(); }
    std::uint64_t position() const noexcept { return position_.load(); }
    std::uint64_t duration() const noexcept { return duration_.load(); }
    std::array<float,32> levels() const noexcept;
private:
    void run(std::filesystem::path file,HWND window,UINT message);
    std::atomic<bool> stop_{false},paused_{false},playing_{false};
    std::atomic<float> volume_{0.8f};
    std::atomic<std::uint64_t> position_{0},duration_{0};
    std::array<std::atomic<float>,10> gains_{};
    std::array<std::atomic<float>,32> levels_{};
    std::thread worker_;
};
}
#endif