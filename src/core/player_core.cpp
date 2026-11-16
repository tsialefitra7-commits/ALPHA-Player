#include "player_core.hpp"
#include <algorithm>
#include <cmath>
namespace alpha {
namespace {
Biquad peak(double fs,double f,double db) {
    constexpr double pi=3.14159265358979323846;
    const double A=std::pow(10.0,db/40.0),w=2*pi*std::min(f,fs*0.45)/fs;
    const double alpha=std::sin(w)/2.0,c=std::cos(w),a0=1+alpha/A;
    return {(1+alpha*A)/a0,-2*c/a0,(1-alpha*A)/a0,-2*c/a0,(1-alpha/A)/a0,0,0};
}
}
float Biquad::process(float x) noexcept { const double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return static_cast<float>(y); }
void Equalizer10::configure(double fs,const std::array<float,10>& gains) {
    if(fs<8000||fs>384000) return;
    for(std::size_t band=0;band<10;++band) {
        const float db=std::clamp(std::isfinite(gains[band])?gains[band]:0.0f,-12.0f,12.0f);
        for(auto& channel:filters_) channel[band]=peak(fs,Frequencies[band],db);
    }
}
void Equalizer10::process(float* data,std::size_t frames,unsigned channels) noexcept {
    if(!data||!channels) return;
    for(std::size_t frame=0;frame<frames;++frame) for(unsigned ch=0;ch<channels;++ch) {
        float& sample=data[frame*channels+ch];
        for(auto& filter:filters_[std::min(ch,7u)]) sample=filter.process(sample);
    }
}
void Visualizer::push(const float* data,std::size_t frames,unsigned channels) noexcept {
    if(!data||!frames||!channels) return;
    double sum=0; const std::size_t count=frames*channels;
    for(std::size_t i=0;i<count;++i) sum+=double(data[i])*data[i];
    for(std::size_t i=31;i>0;--i) levels_[i]=levels_[i-1]*0.9f;
    levels_[0]=std::clamp(static_cast<float>(std::sqrt(sum/count)),0.0f,1.0f);
}
}