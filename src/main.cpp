#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <cwchar>
#include <string>
#include "platform/audio_engine_win32.hpp"
namespace {
constexpr wchar_t ClassName[]=L"ALPHA_PLAYER_WINDOW";
constexpr UINT TimerId=1,FinishedMessage=WM_APP+1;
enum {OpenId=100,PlayId,StopId,VolumeId,EqBase=200};
HWND windowHandle{},volumeControl{};
std::array<HWND,10> eqControls{};
alpha::win32::AudioEngine audio;
std::filesystem::path currentFile;
bool paused=false;
COLORREF bg=RGB(15,17,25),panel=RGB(26,30,43),white=RGB(244,246,255),muted=RGB(145,155,180),cyan=RGB(39,218,225),violet=RGB(151,88,255),pink=RGB(255,82,166);
constexpr std::array<const wchar_t*,10> labels{L"31",L"62",L"125",L"250",L"500",L"1K",L"2K",L"4K",L"8K",L"16K"};
void text(HDC dc,const wchar_t* value,int x,int y,int size,COLORREF color,bool bold=false){
    HFONT f=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);TextOutW(dc,x,y,value,static_cast<int>(wcslen(value)));SelectObject(dc,old);DeleteObject(f);
}
void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void openFile(){
    wchar_t path[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=windowHandle;
    ofn.lpstrFilter=L"Audio\0*.mp3;*.wav;*.flac;*.aac;*.m4a;*.wma;*.ogg;*.opus\0All files\0*.*\0";
    ofn.lpstrFile=path;ofn.nMaxFile=static_cast<DWORD>(std::size(path));ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn))return;
    currentFile=path;paused=false;
    if(!audio.play(currentFile,windowHandle,FinishedMessage))MessageBoxW(windowHandle,L"Le moteur audio n'a pas pu démarrer.",L"ALPHA Player",MB_ICONERROR);
    InvalidateRect(windowHandle,nullptr,FALSE);
}
void setEq(){
    std::array<float,10> gains{};
    for(int i=0;i<10;++i)gains[static_cast<std::size_t>(i)]=static_cast<float>(SendMessageW(eqControls[static_cast<std::size_t>(i)],TBM_GETPOS,0,0));
    audio.setEqualizer(gains);
}
void paint(HWND hwnd){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);fill(dc,r,bg);
    RECT top{0,0,r.right,84};fill(dc,top,RGB(20,23,34));
    text(dc,L"ALPHA",30,20,25,white,true);text(dc,L"PLAYER",119,30,12,cyan,true);
    text(dc,L"YOUR SOUND. YOUR SPACE.",r.right-260,31,11,muted,true);
    RECT card{24,105,r.right-24,r.bottom-24};fill(dc,card,panel);
    text(dc,L"NOW PLAYING",48,132,11,cyan,true);
    const std::wstring title=currentFile.empty()?L"Aucun morceau sélectionné":currentFile.filename().wstring();
    text(dc,title.c_str(),48,166,23,white,true);
    text(dc,L"ÉGALISEUR 10 BANDES",48,280,12,muted,true);
    text(dc,L"-12 dB",48,322,9,muted);text(dc,L"0 dB",48,392,9,muted);text(dc,L"+12 dB",48,460,9,muted);
    for(int i=0;i<10;++i)text(dc,labels[static_cast<std::size_t>(i)],52+i*62,492,9,muted);
    text(dc,L"VISUALISEUR AUDIO",430,280,12,muted,true);
    const auto levels=audio.levels();
    for(std::size_t i=0;i<levels.size();++i){
        const int h=static_cast<int>(std::clamp(levels[i]*130.0f,2.0f,130.0f));
        RECT bar{430+static_cast<int>(i)*17,475-h,442+static_cast<int>(i)*17,475};
        fill(dc,bar,i%3==0?cyan:(i%3==1?violet:pink));
    }
    const auto pos=audio.position()/10000000ULL,dur=audio.duration()/10000000ULL;
    wchar_t time[80]{};swprintf_s(time,L"%02llu:%02llu  /  %02llu:%02llu",
      static_cast<unsigned long long>(pos/60),static_cast<unsigned long long>(pos%60),
      static_cast<unsigned long long>(dur/60),static_cast<unsigned long long>(dur%60));
    text(dc,time,48,r.bottom-90,11,muted);
    text(dc,L"Le signal décodé traverse l'égaliseur avant sa sortie WASAPI.",48,r.bottom-58,10,muted);
    EndPaint(hwnd,&ps);
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        windowHandle=hwnd;INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
        auto make=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){
            return CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        };
        make(L"BUTTON",L"Ouvrir un fichier",BS_PUSHBUTTON,30,195,145,36,OpenId);
        make(L"BUTTON",L"Lecture / Pause",BS_PUSHBUTTON,185,195,145,36,PlayId);
        make(L"BUTTON",L"Stop",BS_PUSHBUTTON,340,195,80,36,StopId);
        volumeControl=make(TRACKBAR_CLASSW,L"",TBS_HORZ,450,195,220,34,VolumeId);
        SendMessageW(volumeControl,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(volumeControl,TBM_SETPOS,TRUE,80);
        for(int i=0;i<10;++i){
            const int x=52+i*62;
            eqControls[static_cast<std::size_t>(i)]=make(TRACKBAR_CLASSW,L"",TBS_VERT|TBS_AUTOTICKS,x,330,34,150,EqBase+i);
            SendMessageW(eqControls[static_cast<std::size_t>(i)],TBM_SETRANGE,TRUE,MAKELONG(-12,12));
            SendMessageW(eqControls[static_cast<std::size_t>(i)],TBM_SETPOS,TRUE,0);
        }
        SetTimer(hwnd,TimerId,80,nullptr);return 0;
    }
    case WM_SIZE:{
        const int width=std::max(760L,LOWORD(lp));
        for(int i=0;i<10;++i)MoveWindow(eqControls[static_cast<std::size_t>(i)],52+i*62,330,34,150,TRUE);
        MoveWindow(volumeControl,450,195,std::min(220,width-480),34,TRUE);return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==OpenId){openFile();return 0;}
        if(LOWORD(wp)==PlayId){
            if(currentFile.empty()){openFile();return 0;}
            if(audio.playing()){paused=!paused;audio.pause(paused);}
            else {paused=false;audio.play(currentFile,hwnd,FinishedMessage);}
            return 0;
        }
        if(LOWORD(wp)==StopId){audio.stop();paused=false;return 0;}
        break;
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(lp)==volumeControl)audio.setVolume(static_cast<float>(SendMessageW(volumeControl,TBM_GETPOS,0,0))/100.0f);
        else setEq();
        return 0;
    case WM_TIMER:InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case FinishedMessage:paused=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_PAINT:paint(hwnd);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_KEYDOWN:
        if(wp==VK_SPACE&&audio.playing()){paused=!paused;audio.pause(paused);return 0;}
        if(wp=='O'&&(GetKeyState(VK_CONTROL)&0x8000)){openFile();return 0;}
        break;
    case WM_DESTROY:KillTimer(hwnd,TimerId);audio.stop();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=nullptr;wc.lpszClassName=ClassName;
    if(!RegisterClassExW(&wc)){CoUninitialize();return 1;}
    HWND hwnd=CreateWindowExW(0,ClassName,L"ALPHA Player",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1120,760,nullptr,nullptr,instance,nullptr);
    if(!hwnd){CoUninitialize();return 1;}ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    CoUninitialize();return static_cast<int>(message.wParam);
}