#ifdef _WIN32
#include "audio_engine_win32.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <ksmedia.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
namespace alpha::win32 {
namespace {
template<class T> void release(T*& p){if(p){p->Release();p=nullptr;}}
struct AudioState {
    IMMDeviceEnumerator* enumerator{}; IMMDevice* device{}; IAudioClient* client{};
    IAudioRenderClient* render{}; IMFSourceReader* reader{}; WAVEFORMATEX* format{};
    ~AudioState(){release(reader);release(render);release(client);release(device);release(enumerator);if(format)CoTaskMemFree(format);}
};
}
AudioEngine::~AudioEngine(){stop();}
bool AudioEngine::play(const std::filesystem::path& file,HWND window,UINT message){
    stop();stop_=false;paused_=false;playing_=true;position_=0;duration_=0;
    try{worker_=std::thread(&AudioEngine::run,this,file,window,message);}catch(...){playing_=false;return false;}
    return true;
}
void AudioEngine::pause(bool value) noexcept {paused_=value;}
void AudioEngine::stop() noexcept {stop_=true;paused_=false;if(worker_.joinable())worker_.join();playing_=false;}
void AudioEngine::seek(std::uint64_t value) noexcept { seekTarget_=value; seekPending_=true; }
void AudioEngine::setVolume(float value) noexcept {volume_=std::clamp(value,0.0f,1.0f);}
void AudioEngine::setEqualizer(const std::array<float,10>& values) noexcept {
    for(std::size_t i=0;i<10;++i)gains_[i]=std::clamp(std::isfinite(values[i])?values[i]:0.0f,-12.0f,12.0f);
}
std::array<float,32> AudioEngine::levels() const noexcept {
    std::array<float,32> result{};for(std::size_t i=0;i<32;++i)result[i]=levels_[i].load();return result;
}
void AudioEngine::run(std::filesystem::path file,HWND window,UINT message){
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(co)){playing_=false;return;}
    const HRESULT mf=MFStartup(MF_VERSION);
    if(FAILED(mf)){CoUninitialize();playing_=false;return;}
    AudioState a;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&a.enumerator));
    if(SUCCEEDED(hr))hr=a.enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&a.device);
    if(SUCCEEDED(hr))hr=a.device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&a.client));
    if(SUCCEEDED(hr))hr=a.client->GetMixFormat(&a.format);
    if(FAILED(hr)||!a.format)goto cleanup;
    {
        const unsigned channels=a.format->nChannels;const unsigned rate=a.format->nSamplesPerSec;
        if(!channels||channels>8||rate<8000)goto cleanup;
        bool floatOut=a.format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        if(a.format->wFormatTag==WAVE_FORMAT_EXTENSIBLE){
            auto* ext=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(a.format);
            floatOut=ext->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            if(!floatOut&&ext->SubFormat!=KSDATAFORMAT_SUBTYPE_PCM)goto cleanup;
        }else if(a.format->wFormatTag!=WAVE_FORMAT_PCM&&!floatOut)goto cleanup;
        hr=a.client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,10000000,0,a.format,nullptr);
        if(SUCCEEDED(hr))hr=a.client->GetService(IID_PPV_ARGS(&a.render));
        IMFAttributes* attrs=nullptr;
        if(SUCCEEDED(hr))hr=MFCreateAttributes(&attrs,1);
        if(SUCCEEDED(hr))hr=attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,0u);
        if(SUCCEEDED(hr))hr=MFCreateSourceReaderFromURL(file.c_str(),attrs,&a.reader);
        release(attrs);
        IMFMediaType* type=nullptr;
        if(SUCCEEDED(hr))hr=MFCreateMediaType(&type);
        if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
        if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);
        if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,channels);
        if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,rate);
        if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,32);
        if(SUCCEEDED(hr))hr=a.reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),nullptr,type);
        release(type);
        if(FAILED(hr))goto cleanup;
        PROPVARIANT d;PropVariantInit(&d);
        if(SUCCEEDED(a.reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&d))&&d.vt==VT_UI8)duration_=d.uhVal.QuadPart;
        PropVariantClear(&d);
        UINT32 capacity=0;if(FAILED(a.client->GetBufferSize(&capacity))||!capacity)goto cleanup;
        if(FAILED(a.client->Start()))goto cleanup;
        {
            Equalizer10 eq;Visualizer visualizer;std::array<float,10> gains{};
            for(std::size_t i=0;i<10;++i)gains[i]=gains_[i].load();
            eq.configure(rate,gains);DWORD flags=0;bool ended=false;
            while(!stop_&&!ended){
                if(paused_){a.client->Stop();Sleep(10);continue;}
                if(FAILED(a.client->Start()))break;
                UINT32 padding=0;if(FAILED(a.client->GetCurrentPadding(&padding)))break;
                if(padding>=capacity){Sleep(3);continue;}
                if(seekPending_.exchange(false)){
                    PROPVARIANT target;PropVariantInit(&target);target.vt=VT_I8;
                    target.hVal.QuadPart=static_cast<LONGLONG>(seekTarget_.load());
                    const HRESULT seekHr=a.reader->SetCurrentPosition(GUID_NULL,target);
                    PropVariantClear(&target);
                    if(SUCCEEDED(seekHr))position_=seekTarget_.load();
                }
                IMFSample* sample=nullptr;LONGLONG stamp=0;
                hr=a.reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),0,nullptr,&flags,&stamp,&sample);
                if(FAILED(hr)){release(sample);break;}
                if(flags&MF_SOURCE_READERF_ENDOFSTREAM){release(sample);ended=true;break;}
                if(!sample){Sleep(2);continue;}
                IMFMediaBuffer* buffer=nullptr;hr=sample->ConvertToContiguousBuffer(&buffer);release(sample);
                if(FAILED(hr)||!buffer){release(buffer);break;}
                BYTE* bytes=nullptr;DWORD max=0,length=0;hr=buffer->Lock(&bytes,&max,&length);
                if(FAILED(hr)){release(buffer);break;}
                if(length>max){buffer->Unlock();release(buffer);break;}
                const std::size_t frames=length/(sizeof(float)*channels);
                std::vector<float> pcm(frames*channels);
                if(frames)std::memcpy(pcm.data(),bytes,pcm.size()*sizeof(float));
                buffer->Unlock();release(buffer);
                if(!frames)continue;
                bool changed=false;
                for(std::size_t i=0;i<10;++i){const float v=gains_[i].load();if(v!=gains[i])changed=true;gains[i]=v;}
                if(changed)eq.configure(rate,gains);
                eq.process(pcm.data(),frames,channels);
                for(float& v:pcm){v=std::clamp(v,-1.0f,1.0f);v*=volume_.load();}
                visualizer.push(pcm.data(),frames,channels);
                const auto visualLevels=visualizer.levels();
                for(std::size_t i=0;i<32;++i)levels_[i]=visualLevels[i];
                position_=static_cast<std::uint64_t>(std::max<LONGLONG>(0,stamp));
                std::size_t offset=0;
                while(offset<frames&&!stop_){
                    if(paused_){a.client->Stop();while(paused_&&!stop_)Sleep(10);if(!stop_)a.client->Start();}
                    UINT32 pad=0;if(FAILED(a.client->GetCurrentPadding(&pad)))break;
                    const UINT32 room=capacity>pad?capacity-pad:0;if(!room){Sleep(2);continue;}
                    const UINT32 count=static_cast<UINT32>(std::min<std::size_t>(room,frames-offset));
                    BYTE* dst=nullptr;if(FAILED(a.render->GetBuffer(count,&dst)))break;
                    const std::size_t n=static_cast<std::size_t>(count)*channels;const WORD bits=a.format->wBitsPerSample;
                    if(floatOut&&bits==32)std::memcpy(dst,pcm.data()+offset*channels,n*sizeof(float));
                    else if(bits==16){auto* out=reinterpret_cast<std::int16_t*>(dst);for(std::size_t i=0;i<n;++i)out[i]=static_cast<std::int16_t>(pcm[offset*channels+i]*32767.0f);}
                    else if(bits==24){for(std::size_t i=0;i<n;++i){const auto v=static_cast<std::int32_t>(pcm[offset*channels+i]*8388607.0f);dst[i*3]=static_cast<BYTE>(v);dst[i*3+1]=static_cast<BYTE>(v>>8);dst[i*3+2]=static_cast<BYTE>(v>>16);}}
                    else if(bits==32){auto* out=reinterpret_cast<std::int32_t*>(dst);for(std::size_t i=0;i<n;++i)out[i]=static_cast<std::int32_t>(pcm[offset*channels+i]*2147483647.0);}
                    else {a.render->ReleaseBuffer(count,AUDCLNT_BUFFERFLAGS_SILENT);break;}
                    if(FAILED(a.render->ReleaseBuffer(count,0)))break;offset+=count;
                }
            }
            a.client->Stop();
            if(ended&&!stop_&&window)PostMessageW(window,message,0,0);
        }
    }
cleanup:
    playing_=false;if(a.client)a.client->Stop();
    release(a.reader);release(a.render);release(a.client);release(a.device);release(a.enumerator);
    if(a.format){CoTaskMemFree(a.format);a.format=nullptr;}
    MFShutdown();CoUninitialize();
}
}
#endif