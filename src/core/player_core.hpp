#pragma once
#include <array>
#include <cstddef>
namespace alpha {
struct Biquad {
    double b0{1},b1{},b2{},a1{},a2{},z1{},z2{};
    float process(float x) noexcept;
};
class Equalizer10 {
public:
    static constexpr std::array<double,10> Frequencies{31.25,62.5,125,250,500,1000,2000,4000,8000,16000};
    void configure(double sampleRate,const std::array<float,10>& gainsDb);
    void process(float* interleaved,std::size_t frames,unsigned channels) noexcept;
private:
    std::array<std::array<Biquad,10>,8> filters_{};
};
class Visualizer {
public:
    void push(const float* interleaved,std::size_t frames,unsigned channels) noexcept;
    const std::array<float,32>& levels() const noexcept { return levels_; }
private:
    std::array<float,32> levels_{};
};
}