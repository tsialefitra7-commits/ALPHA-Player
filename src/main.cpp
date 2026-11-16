#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfplay.h>
#include <wrl/client.h>
#include <string>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kClassName[] = L"ALPHA_PLAYER_WINDOW";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerIntervalMs = 250;

struct Palette {
    COLORREF background = RGB(15, 17, 25);
    COLORREF panel = RGB(25, 28, 40);
    COLORREF panelLight = RGB(34, 38, 54);
    COLORREF text = RGB(244, 246, 255);
    COLORREF muted = RGB(143, 151, 174);
    COLORREF blue = RGB(49, 132, 255);
    COLORREF violet = RGB(145, 82, 255);
    COLORREF cyan = RGB(36, 220, 220);
    COLORREF pink = RGB(255, 75, 163);
};
Palette g_colors;

class PlayerCallback final : public IMFPMediaPlayerCallback {
public:
    explicit PlayerCallback(HWND hwnd) : hwnd_(hwnd) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_;
        if (!n) delete this;
        return n;
    }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        if (event) PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(event->eEventType), 0);
    }
private:
    volatile LONG refs_ = 1;
    HWND hwnd_ = nullptr;
};

HWND g_hwnd = nullptr;
IMFPMediaPlayer* g_player = nullptr;
std::wstring g_fileName;
bool g_isPlaying = false;
bool g_isDragging = false;
float g_volume = 0.78f;
double g_duration = 0.0;
double g_position = 0.0;

std::wstring BaseName(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring TimeText(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int total = static_cast<int>(seconds);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02d:%02d", total / 60, total % 60);
    return buffer;
}

void Fill(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void RoundRectFill(HDC dc, const RECT& r, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Text(HDC dc, const wchar_t* value, int x, int y, int size,
          COLORREF color, bool bold = false) {
    HFONT font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, value, static_cast<int>(wcslen(value)));
    SelectObject(dc, old);
    DeleteObject(font);
}

void Gradient(HDC dc, RECT r, COLORREF a, COLORREF b, bool vertical = false) {
    TRIVERTEX vertices[2] = {
        {r.left, r.top, static_cast<COLOR16>(GetRValue(a) << 8),
         static_cast<COLOR16>(GetGValue(a) << 8), static_cast<COLOR16>(GetBValue(a) << 8), 0xffff},
        {r.right, r.bottom, static_cast<COLOR16>(GetRValue(b) << 8),
         static_cast<COLOR16>(GetGValue(b) << 8), static_cast<COLOR16>(GetBValue(b) << 8), 0xffff}
    };
    GRADIENT_RECT mesh{0, 1};
    GradientFill(dc, vertices, 2, &mesh, 1, vertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
}

RECT ProgressRect(const RECT& client) {
    return RECT{40, client.bottom - 142, client.right - 40, client.bottom - 134};
}

void DrawButton(HDC dc, RECT r, const wchar_t* glyph, bool primary = false) {
    RoundRectFill(dc, r, 16, primary ? g_colors.blue : g_colors.panelLight);
    Text(dc, glyph, r.left + (r.right-r.left)/2 - 7,
         r.top + (r.bottom-r.top)/2 - 11, 20, g_colors.text, true);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    Fill(dc, client, g_colors.background);

    // Top navigation and brand
    RECT top{0, 0, client.right, 76};
    Fill(dc, top, RGB(19, 21, 31));
    RECT logo{34, 22, 66, 54};
    Gradient(dc, logo, g_colors.blue, g_colors.violet);
    RoundRectFill(dc, logo, 9, g_colors.violet);
    Text(dc, L"A", 43, 25, 23, RGB(255,255,255), true);
    Text(dc, L"ALPHA", 78, 20, 21, g_colors.text, true);
    Text(dc, L"PLAYER", 78, 43, 10, g_colors.cyan, true);
    Text(dc, L"YOUR SOUND. YOUR SPACE.", client.right - 250, 31, 10, g_colors.muted, true);

    // Main content panel
    RECT panel{28, 100, client.right - 28, client.bottom - 176};
    RoundRectFill(dc, panel, 22, g_colors.panel);
    Text(dc, L"NOW PLAYING", 58, 126, 11, g_colors.cyan, true);

    // Album artwork placeholder
    RECT art{58, 164, 270, 376};
    Gradient(dc, art, RGB(39, 110, 255), RGB(151, 55, 230), true);
    RoundRectFill(dc, art, 20, RGB(89, 75, 230));
    RECT inner{80, 186, 248, 354};
    Gradient(dc, inner, RGB(38, 205, 219), RGB(111, 55, 226), true);
    RoundRectFill(dc, inner, 16, RGB(76, 106, 235));
    Text(dc, L"ALPHA", 108, 242, 25, RGB(255,255,255), true);
    Text(dc, L"SOUND", 116, 274, 12, RGB(225,240,255), true);

    Text(dc, L"TRACK TITLE", 304, 186, 11, g_colors.muted, true);
    const std::wstring title = g_fileName.empty() ? L"Nothing playing yet" : BaseName(g_fileName);
    Text(dc, title.c_str(), 304, 213, 24, g_colors.text, true);
    Text(dc, g_fileName.empty() ? L"Open a file to start listening" : L"Local audio file",
         304, 250, 13, g_colors.muted);

    // Decorative sound bars
    const int bars[] = {22, 42, 30, 58, 76, 40, 64, 28, 50, 70, 34, 56, 24, 46, 66, 32};
    int bx = 306;
    for (int i = 0; i < 16; ++i) {
        RECT bar{bx, 328 - bars[i]/2, bx + 7, 328 + bars[i]/2};
        COLORREF c = (i % 3 == 0) ? g_colors.cyan : ((i % 3 == 1) ? g_colors.blue : g_colors.violet);
        RoundRectFill(dc, bar, 5, c);
        bx += 13;
    }

    // Playback panel
    RECT controls{28, client.bottom - 158, client.right - 28, client.bottom - 22};
    RoundRectFill(dc, controls, 20, RGB(22, 25, 37));
    RECT progress = ProgressRect(client);
    RoundRectFill(dc, progress, 5, RGB(59, 64, 82));
    const double ratio = g_duration > 0 ? std::clamp(g_position / g_duration, 0.0, 1.0) : 0.0;
    RECT played = progress;
    played.right = played.left + static_cast<LONG>((played.right-played.left)*ratio);
    if (played.right > played.left) RoundRectFill(dc, played, 5, g_colors.cyan);

    Text(dc, TimeText(g_position).c_str(), 40, client.bottom - 124, 10, g_colors.muted);
    const std::wstring total = TimeText(g_duration);
    SIZE sz{};
    GetTextExtentPoint32W(dc, total.c_str(), static_cast<int>(total.size()), &sz);
    Text(dc, total.c_str(), client.right - 40 - sz.cx, client.bottom - 124, 10, g_colors.muted);

    DrawButton(dc, RECT{client.right/2 - 84, client.bottom - 91, client.right/2 - 36, client.bottom - 43}, L"■");
    DrawButton(dc, RECT{client.right/2 - 25, client.bottom - 98, client.right/2 + 39, client.bottom - 34},
               g_isPlaying ? L"Ⅱ" : L"▶", true);
    DrawButton(dc, RECT{client.right/2 + 50, client.bottom - 91, client.right/2 + 98, client.bottom - 43}, L"↻");

    Text(dc, L"VOLUME", client.right - 230, client.bottom - 91, 9, g_colors.muted, true);
    RECT vol{client.right - 230, client.bottom - 65, client.right - 60, client.bottom - 59};
    RoundRectFill(dc, vol, 4, RGB(59,64,82));
    RECT volFill = vol;
    volFill.right = vol.left + static_cast<LONG>((vol.right-vol.left)*g_volume);
    RoundRectFill(dc, volFill, 4, g_colors.pink);

    Text(dc, L"OPEN AUDIO FILE", 46, client.bottom - 205, 10, g_colors.muted, true);
    Text(dc, L"MP3  WAV  FLAC  AAC  WMA  OGG", 46, client.bottom - 187, 9, g_colors.muted);
    EndPaint(hwnd, &ps);
}

void UpdatePosition() {
    if (!g_player || g_isDragging) return;
    MFTIME pos = 0, dur = 0;
    if (SUCCEEDED(g_player->GetPosition(&pos))) g_position = static_cast<double>(pos) / 10000000.0;
    if (SUCCEEDED(g_player->GetDuration(&dur))) g_duration = static_cast<double>(dur) / 10000000.0;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void OpenAudio(HWND hwnd) {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Audio files\0*.mp3;*.wav;*.flac;*.aac;*.wma;*.m4a;*.ogg\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Open audio file";
    if (!GetOpenFileNameW(&ofn)) return;

    g_fileName = path;
    g_position = 0;
    g_duration = 0;
    g_isPlaying = false;
    if (!g_player) {
        auto* callback = new PlayerCallback(hwnd);
        HRESULT hr = MFCreateMediaPlayer(nullptr, TRUE, MFP_OPTION_NONE, callback, hwnd, &g_player);
        callback->Release();
        if (FAILED(hr)) {
            MessageBoxW(hwnd, L"Media Foundation could not initialize the audio player.", L"ALPHA Player", MB_ICONERROR);
            return;
        }
    }
    IMFPMediaItem* item = nullptr;
    HRESULT hr = g_player->CreateMediaItemFromURL(path, FALSE, 0, &item);
    if (SUCCEEDED(hr) && item) {
        g_player->SetMediaItem(item);
        item->Release();
    } else {
        MessageBoxW(hwnd, L"This audio file could not be opened by Windows Media Foundation.",
                    L"ALPHA Player", MB_ICONWARNING);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        MFStartup(MF_VERSION);
        SetTimer(hwnd, kTimerId, kTimerIntervalMs, nullptr);
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        UpdatePosition();
        return 0;
    case WM_APP + 1:
        if (wp == MFP_EVENT_TYPE_MEDIAITEM_SET) {
            if (g_player) {
                g_player->Play();
                g_isPlaying = true;
            }
        } else if (wp == MFP_EVENT_TYPE_PLAY) {
            g_isPlaying = true;
        } else if (wp == MFP_EVENT_TYPE_PAUSE || wp == MFP_EVENT_TYPE_STOP) {
            g_isPlaying = false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT client{}; GetClientRect(hwnd, &client);
        if (y > client.bottom - 158 && y < client.bottom - 22) {
            if (x > client.right/2 - 25 && x < client.right/2 + 39 && y > client.bottom - 105) {
                if (g_player) {
                    if (g_isPlaying) g_player->Pause();
                    else g_player->Play();
                } else OpenAudio(hwnd);
            } else if (x > client.right/2 - 90 && x < client.right/2 - 30 && g_player) {
                g_player->Stop();
                g_isPlaying = false;
                g_position = 0;
            } else if (x > client.right - 240 && x < client.right - 50 && y > client.bottom - 80) {
                g_volume = std::clamp((x - (client.right - 230)) / 170.0f, 0.0f, 1.0f);
                if (g_player) g_player->SetVolume(g_volume);
            } else {
                RECT p = ProgressRect(client);
                if (g_player && x >= p.left && x <= p.right && g_duration > 0) {
                    g_isDragging = true;
                    g_position = std::clamp((x - p.left) / static_cast<double>(p.right-p.left), 0.0, 1.0) * g_duration;
                    MFTIME target = static_cast<MFTIME>(g_position * 10000000.0);
                    g_player->SetPosition(&target);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (y > 100 && y < client.bottom - 176) {
            OpenAudio(hwnd);
        }
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_isDragging && g_player) {
            RECT client{}; GetClientRect(hwnd, &client);
            RECT p = ProgressRect(client);
            const int x = GET_X_LPARAM(lp);
            g_position = std::clamp((x-p.left)/static_cast<double>(p.right-p.left), 0.0, 1.0) * g_duration;
            MFTIME target = static_cast<MFTIME>(g_position * 10000000.0);
            g_player->SetPosition(&target);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        g_isDragging = false;
        ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE && g_player) {
            if (g_isPlaying) g_player->Pause(); else g_player->Play();
        } else if (wp == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            OpenAudio(hwnd);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        if (g_player) { g_player->Shutdown(); g_player->Release(); g_player = nullptr; }
        MFShutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kClassName, L"ALPHA Player",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
