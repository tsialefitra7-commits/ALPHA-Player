#include "core/player_core.hpp"
#include <array>
#include <cassert>
#include <cmath>
int main(){
    alpha::Equalizer10 flat,boost;
    std::array<float,10> zero{},gain{};gain[5]=9.0f;
    flat.configure(48000,zero);boost.configure(48000,gain);
    std::array<float,2048> a{},b{};
    constexpr double pi=3.14159265358979323846;
    for(std::size_t i=0;i<1024;++i){
        const float s=0.2f*static_cast<float>(std::sin(2*pi*1000.0*static_cast<double>(i)/48000.0));
        a[i*2]=s;a[i*2+1]=s;b[i*2]=s;b[i*2+1]=s;
    }
    flat.process(a.data(),1024,2);boost.process(b.data(),1024,2);
    double ea=0,eb=0;
    for(std::size_t i=512;i<a.size();++i){ea+=a[i]*a[i];eb+=b[i]*b[i];}
    assert(eb>ea*1.2);
    alpha::Visualizer visual;
    visual.push(b.data(),1024,2);
    assert(visual.levels()[0]>0.0f&&visual.levels()[0]<=1.0f);
}