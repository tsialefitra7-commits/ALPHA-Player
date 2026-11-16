#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <random>
#include "platform/audio_engine_win32.hpp"
namespace {
constexpr wchar_t ClassName[]=L"ALPHA_PLAYER_WINDOW";
constexpr UINT TimerId=1,FinishedMessage=WM_APP+1;
enum {AddId=100,PlayId,StopId,PrevId,NextId,RepeatId,ShuffleId,VolumeId,ProgressId,ListId,EqBase=200};
HWND windowHandle{},volumeControl{},progressControl{},playlistControl{};
std::array<HWND,10> eqControls{};
alpha::win32::AudioEngine audio;
std::vector<std::filesystem::path> playlist;
int currentIndex=-1;
bool paused=false,repeat=false,shuffle=false,seeking=false;
std::mt19937 randomEngine{std::random_device{}()};
std::filesystem::path settingsPath;
struct Metadata { std::wstring title,artist,album; } currentMetadata;
COLORREF bg=RGB(15,17,25),panel=RGB(26,30,43),white=RGB(244,246,255),muted=RGB(145,155,180),cyan=RGB(39,218,225),violet=RGB(151,88,255),pink=RGB(255,82,166);
constexpr std::array<const wchar_t*,10> labels{L"31",L"62",L"125",L"250",L"500",L"1K",L"2K",L"4K",L"8K",L"16K"};
std::wstring readString(IMFSourceReader* reader,const GUID& key){
    PROPVARIANT v;PropVariantInit(&v);std::wstring out;
    if(SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),key,&v))){
        if(v.vt==VT_LPWSTR&&v.pwszVal)out=v.pwszVal;
        else if(v.vt==VT_BSTR&&v.bstrVal)out=v.bstrVal;
    }
    PropVariantClear(&v);return out;
}
Metadata metadataFor(const std::filesystem::path& file){
    Metadata m;m.title=file.stem().wstring();
    if(FAILED(MFStartup(MF_VERSION)))return m;
    IMFSourceReader* reader=nullptr;
    if(SUCCEEDED(MFCreateSourceReaderFromURL(file.c_str(),nullptr,&reader))){
        auto s=readString(reader,MF_PD_TITLE);if(!s.empty())m.title=s;
        m.artist=readString(reader,MF_PD_ARTIST);m.album=readString(reader,MF_PD_ALBUM);
        reader->Release();
    }
    MFShutdown();return m;
}
void text(HDC dc,const wchar_t* value,int x,int y,int size,COLORREF color,bool bold=false){
    HFONT f=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);TextOutW(dc,x,y,value,static_cast<int>(wcslen(value)));SelectObject(dc,old);DeleteObject(f);
}
void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void saveEq(){
    for(int i=0;i<10;++i){wchar_t key[16]{},value[16]{};swprintf_s(key,L"Band%d",i);swprintf_s(value,L"%d",static_cast<int>(SendMessageW(eqControls[i],TBM_GETPOS,0,0)));WritePrivateProfileStringW(L"Equalizer",key,value,settingsPath.c_str());}
}
void loadEq(){
    for(int i=0;i<10;++i){wchar_t key[16]{};swprintf_s(key,L"Band%d",i);int v=GetPrivateProfileIntW(L"Equalizer",key,0,settingsPath.c_str());SendMessageW(eqControls[i],TBM_SETPOS,TRUE,std::clamp(v,-12,12));}
}
void setEq(){std::array<float,10> gains{};for(int i=0;i<10;++i)gains[i]=static_cast<float>(SendMessageW(eqControls[i],TBM_GETPOS,0,0));audio.setEqualizer(gains);}
void updateList(){SendMessageW(playlistControl,LB_RESETCONTENT,0,0);for(const auto& p:playlist){auto s=p.filename().wstring();SendMessageW(playlistControl,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));}if(currentIndex>=0)SendMessageW(playlistControl,LB_SETCURSEL,currentIndex,0);}
void playIndex(int index){
    if(playlist.empty())return;
    if(index<0||index>=static_cast<int>(playlist.size()))return;
    currentIndex=index;paused=false;currentMetadata=metadataFor(playlist[currentIndex]);
    if(!audio.play(playlist[currentIndex],windowHandle,FinishedMessage))MessageBoxW(windowHandle,L"Impossible de démarrer le moteur audio.",L"ALPHA Player",MB_ICONERROR);
    updateList();InvalidateRect(windowHandle,nullptr,FALSE);
}
void addFiles(){
    std::vector<wchar_t> buffer(65536);OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=windowHandle;
    ofn.lpstrFilter=L"Fichiers audio\0*.mp3;*.wav;*.flac;*.aac;*.m4a;*.wma;*.ogg;*.opus\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile=buffer.data();ofn.nMaxFile=static_cast<DWORD>(buffer.size());ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER;
    if(!GetOpenFileNameW(&ofn))return;
    const wchar_t* first=buffer.data();std::filesystem::path folder=first;const wchar_t* p=first+wcslen(first)+1;
    if(!*p)playlist.push_back(folder);
    else while(*p){playlist.push_back(folder/p);p+=wcslen(p)+1;}
    if(currentIndex<0&&!playlist.empty())currentIndex=0;
    updateList();if(!audio.playing()&&currentIndex>=0)playIndex(currentIndex);
}
void nextTrack(){
    if(playlist.empty())return;
    int next=currentIndex;
    if(shuffle&&playlist.size()>1){std::uniform_int_distribution<int>d(0,static_cast<int>(playlist.size()-1));do{next=d(randomEngine);}while(next==currentIndex);}
    else next=currentIndex+1;
    if(next>=static_cast<int>(playlist.size())){if(repeat)next=0;else return;}
    playIndex(next);
}
void previousTrack(){if(playlist.empty())return;if(audio.position()>30000000ULL){audio.seek(0);return;}int p=currentIndex-1;if(p<0)p=repeat?static_cast<int>(playlist.size()-1):0;playIndex(p);}
void paint(HWND hwnd){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);fill(dc,r,bg);
    RECT top{0,0,r.right,76};fill(dc,top,RGB(20,23,34));
    text(dc,L"ALPHA",26,17,25,white,true);text(dc,L"PLAYER",115,27,12,cyan,true);
    RECT card{20,90,r.right-20,r.bottom-18};fill(dc,card,panel);
    text(dc,L"LECTURE EN COURS",42,108,11,cyan,true);
    std::wstring title=L"Aucun morceau";std::wstring artist,album;
    if(currentIndex>=0&&currentIndex<static_cast<int>(playlist.size())){title=currentMetadata.title;artist=currentMetadata.artist;album=currentMetadata.album;}
    text(dc,title.c_str(),42,132,20,white,true);
    std::wstring details=(artist.empty()?L"Artiste inconnu":artist)+L"   •   "+(album.empty()?L"Album inconnu":album);
    text(dc,details.c_str(),42,160,10,muted);
    const auto pos=audio.position()/10000000ULL,dur=audio.duration()/10000000ULL;
    wchar_t time[64]{};swprintf_s(time,L"%02llu:%02llu / %02llu:%02llu",static_cast<unsigned long long>(pos/60),static_cast<unsigned long long>(pos%60),static_cast<unsigned long long>(dur/60),static_cast<unsigned long long>(dur%60));
    text(dc,time,42,211,10,muted);
    text(dc,L"PLAYLIST",720,108,11,cyan,true);
    text(dc,L"ÉGALISEUR 10 BANDES",42,286,11,muted,true);
    for(int i=0;i<10;++i)text(dc,labels[i],44+i*62,487,9,muted);
    text(dc,L"VISUALISEUR AUDIO",430,286,11,muted,true);
    const auto levels=audio.levels();for(std::size_t i=0;i<levels.size();++i){int h=static_cast<int>(std::clamp(levels[i]*125.0f,2.0f,125.0f));RECT b{430+static_cast<int>(i)*8,465-h,436+static_cast<int>(i)*8,465};fill(dc,b,i%3==0?cyan:(i%3==1?violet:pink));}
    EndPaint(hwnd,&ps);
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{
    windowHandle=hwnd;INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);settingsPath=std::filesystem::path(exe).parent_path()/L"alpha-player.ini";
    auto make=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){return CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);};
    make(L"BUTTON",L"Ajouter des fichiers",BS_PUSHBUTTON,42,180,150,30,AddId);
    make(L"BUTTON",L"Précédent",BS_PUSHBUTTON,202,180,90,30,PrevId);
    make(L"BUTTON",L"Lecture / Pause",BS_PUSHBUTTON,300,180,120,30,PlayId);
    make(L"BUTTON",L"Suivant",BS_PUSHBUTTON,428,180,80,30,NextId);
    make(L"BUTTON",L"Stop",BS_PUSHBUTTON,516,180,65,30,StopId);
    make(L"BUTTON",L"Répéter : non",BS_PUSHBUTTON,590,180,105,30,RepeatId);
    make(L"BUTTON",L"Aléatoire : non",BS_PUSHBUTTON,700,180,110,30,ShuffleId);
    volumeControl=make(TRACKBAR_CLASSW,L"",TBS_HORZ,820,180,170,30,VolumeId);SendMessageW(volumeControl,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(volumeControl,TBM_SETPOS,TRUE,80);
    progressControl=make(TRACKBAR_CLASSW,L"",TBS_HORZ,42,230,650,28,ProgressId);SendMessageW(progressControl,TBM_SETRANGE,TRUE,MAKELONG(0,1000));
    playlistControl=make(L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,720,130,330,470,ListId);
    for(int i=0;i<10;++i){eqControls[i]=make(TRACKBAR_CLASSW,L"",TBS_VERT|TBS_AUTOTICKS,42+i*62,330,34,145,EqBase+i);SendMessageW(eqControls[i],TBM_SETRANGE,TRUE,MAKELONG(-12,12));}
    loadEq();setEq();SetTimer(hwnd,TimerId,100,nullptr);return 0;
 }
 case WM_COMMAND:
    switch(LOWORD(wp)){
    case AddId:addFiles();return 0;
    case PlayId:if(currentIndex<0){addFiles();return 0;}if(audio.playing()){paused=!paused;audio.pause(paused);}else playIndex(currentIndex);return 0;
    case StopId:audio.stop();paused=false;return 0;
    case PrevId:previousTrack();return 0;
    case NextId:nextTrack();return 0;
    case RepeatId:repeat=!repeat;SetWindowTextW(GetDlgItem(hwnd,RepeatId),repeat?L"Répéter : oui":L"Répéter : non");return 0;
    case ShuffleId:shuffle=!shuffle;SetWindowTextW(GetDlgItem(hwnd,ShuffleId),shuffle?L"Aléatoire : oui":L"Aléatoire : non");return 0;
    case ListId:if(HIWORD(wp)==LBN_DBLCLK){int n=static_cast<int>(SendMessageW(playlistControl,LB_GETCURSEL,0,0));playIndex(n);}return 0;
    }break;
 case WM_HSCROLL:
    if(reinterpret_cast<HWND>(lp)==volumeControl)audio.setVolume(static_cast<float>(SendMessageW(volumeControl,TBM_GETPOS,0,0))/100.0f);
    else if(reinterpret_cast<HWND>(lp)==progressControl&&audio.duration()>0){const int p=static_cast<int>(SendMessageW(progressControl,TBM_GETPOS,0,0));audio.seek(audio.duration()*static_cast<std::uint64_t>(p)/1000ULL);}
    else {setEq();saveEq();}return 0;
 case WM_TIMER:{
    const auto d=audio.duration(),p=audio.position();if(d>0&&GetCapture()!=progressControl)SendMessageW(progressControl,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::min<std::uint64_t>(1000,p*1000/d)));
    InvalidateRect(hwnd,nullptr,FALSE);return 0;
 }
 case FinishedMessage:paused=false;if(repeat)playIndex(currentIndex);else nextTrack();return 0;
 case WM_KEYDOWN:if(wp==VK_SPACE&&audio.playing()){paused=!paused;audio.pause(paused);return 0;}if(wp==VK_RIGHT)nextTrack();if(wp==VK_LEFT)previousTrack();break;
 case WM_PAINT:paint(hwnd);return 0;
 case WM_ERASEBKGND:return 1;
 case WM_DESTROY:saveEq();KillTimer(hwnd,TimerId);audio.stop();PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=nullptr;wc.lpszClassName=ClassName;
 if(!RegisterClassExW(&wc)){CoUninitialize();return 1;}HWND hwnd=CreateWindowExW(0,ClassName,L"ALPHA Player",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1100,700,nullptr,nullptr,instance,nullptr);
 if(!hwnd){CoUninitialize();return 1;}ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}CoUninitialize();return static_cast<int>(m.wParam);
}