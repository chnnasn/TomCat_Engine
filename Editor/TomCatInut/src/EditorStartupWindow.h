#pragma once

// This window must not depend on GLFW, ImGui, fonts or extracted package files.
#ifdef TC_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <shellapi.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include "TomCat/Core/Version.h"
#include "TomCat/Core/ApplicationPaths.h"
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shell32.lib")

namespace TomCat {
class EditorStartupWindow final {
public:
    static EditorStartupWindow& Get() { static EditorStartupWindow instance; return instance; }
    void Start(const std::filesystem::path& project) {
        if (m_Thread.joinable()) return;
        m_Project = project.empty() ? L"TomCat Editor" : project.parent_path().filename().wstring();
        if (m_Project.empty()) m_Project = project.stem().wstring();
        m_Log = ApplicationPaths::GetLogFile(ApplicationProduct::Editor).value_or(std::filesystem::path{});
        m_Thread = std::thread([this] { Run(); });
        std::unique_lock lock(m_Mutex);
        m_Ready.wait(lock, [this] { return m_Created; });
    }
    void SetProject(std::wstring project) { std::lock_guard lock(m_Mutex); m_Project = std::move(project); }
    void Update(std::wstring stage, uint64_t processed = 0, uint64_t total = 0) {
        std::lock_guard lock(m_Mutex);
        m_Stage = std::move(stage); m_Processed = processed; m_Total = total;
    }
    void Close() {
        HWND window;
        { std::lock_guard lock(m_Mutex); window = m_Window; }
        if (window) PostMessageW(window, WM_APP + 1, 0, 0);
        if (m_Thread.joinable()) m_Thread.join();
    }
    void Fail(const char* error) {
        if (!m_Thread.joinable()) return;
        const int count = MultiByteToWideChar(CP_UTF8, 0, error, -1, nullptr, 0);
        std::wstring message(count > 0 ? count : 1, L'\0');
        if (count > 0) MultiByteToWideChar(CP_UTF8, 0, error, -1, message.data(), count);
        {
            std::lock_guard lock(m_Mutex);
            m_Error = true; m_Stage = L"启动失败"; m_Detail = std::move(message);
        }
        // Keep the reason and log action visible until the user dismisses it.
        m_Thread.join();
    }
    ~EditorStartupWindow() { Close(); }
private:
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<EditorStartupWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<EditorStartupWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        switch (message) {
        case WM_TIMER: {
            bool failed;
            { std::lock_guard lock(self->m_Mutex); failed = self->m_Error; }
            if (failed && !self->m_LogControl) {
                RECT client{}; GetClientRect(window, &client);
                const auto button = [&](const wchar_t* label, int id, int x, int width) {
                    return CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        MulDiv(x, client.right, 760), MulDiv(433, client.bottom, 470),
                        MulDiv(width, client.right, 760), MulDiv(31, client.bottom, 470),
                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
                };
                self->m_LogControl = button(L"打开启动日志(&L)", 1001, 26, 175);
                self->m_CloseControl = button(L"关闭(&C)", 1002, 635, 100);
                SetFocus(self->m_LogControl);
            }
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == 1001) { self->OpenLog(window); return 0; }
            if (LOWORD(w) == 1002) { DestroyWindow(window); return 0; }
            break;
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (item->CtlID != 1001 && item->CtlID != 1002) break;
            HBRUSH brush = CreateSolidBrush(RGB(24, 24, 22));
            FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, item->CtlID == 1001 ? RGB(243, 239, 229) : RGB(243, 239, 229));
            HFONT font = CreateFontW(-MulDiv(16, GetDpiForWindow(window), 96), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            auto previous = SelectObject(item->hDC, font);
            wchar_t label[80]{}; GetWindowTextW(item->hwndItem, label, 80);
            RECT rect = item->rcItem;
            DrawTextW(item->hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &rect);
            SelectObject(item->hDC, previous); DeleteObject(font); return TRUE;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self->Paint(window); return 0;
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(window, message, w, l);
            POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
            ScreenToClient(window, &point);
            RECT client{}; GetClientRect(window, &client);
            point.y = MulDiv(point.y, 470, std::max(1L, client.bottom));
            return hit == HTCLIENT && point.y < 220 ? HTCAPTION : hit;
        }
        case WM_CLOSE: {
            bool failed;
            { std::lock_guard lock(self->m_Mutex); failed = self->m_Error; }
            if (failed) DestroyWindow(window);
            return 0;
        }
        case WM_APP + 1: DestroyWindow(window); return 0;
        case WM_DESTROY: {
            std::lock_guard lock(self->m_Mutex); self->m_Window = nullptr;
            PostQuitMessage(0); return 0;
        }
        }
        return DefWindowProcW(window, message, w, l);
    }
    void OpenLog(HWND window) {
        if (m_Log.empty()) { MessageBoxW(window, L"启动日志路径不可用", L"TomCat", MB_OK | MB_ICONINFORMATION); return; }
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        wchar_t systemDirectory[MAX_PATH]{}; GetSystemDirectoryW(systemDirectory, MAX_PATH);
        const auto viewer = std::filesystem::path(systemDirectory) / L"notepad.exe";
        const std::wstring argument = L"\"" + m_Log.wstring() + L"\"";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", viewer.c_str(), argument.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
            MessageBoxW(window, m_Log.c_str(), L"日志路径（无法自动打开）", MB_OK | MB_ICONINFORMATION);
    }
    void Run() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HINSTANCE module = GetModuleHandleW(nullptr);
        WNDCLASSW type{};
        type.lpfnWndProc = Procedure; type.hInstance = module;
        type.lpszClassName = L"TomCatStartupWindow";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(module, MAKEINTRESOURCEW(101));
        RegisterClassW(&type);
        RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        const double dpiScale = GetDpiForSystem() / 96.0;
        const double scale = std::min({dpiScale, (area.right - area.left) / 760.0,
            (area.bottom - area.top) / 470.0});
        const int width = static_cast<int>(760 * scale);
        const int height = static_cast<int>(470 * scale);
        HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, type.lpszClassName,
            L"TomCat · 正在启动", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
            area.left + (area.right - area.left - width) / 2,
            area.top + (area.bottom - area.top - height) / 2,
            width, height, nullptr, nullptr, module, this);
        if (window) {
            ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
            SetTimer(window, 1, 40, nullptr);
        }
        { std::lock_guard lock(m_Mutex); m_Window = window; m_Created = true; }
        m_Ready.notify_one();
        if (window) {
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
        }
        UnregisterClassW(type.lpszClassName, module);
    }
    void Paint(HWND window) {
        std::wstring stage, detail, project; uint64_t processed, total; bool failed;
        { std::lock_guard lock(m_Mutex);
          stage = m_Stage; detail = m_Detail; project = m_Project; processed = m_Processed; total = m_Total; failed = m_Error; }
        PAINTSTRUCT paint{}; HDC target = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const int width = 760, height = 470;
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        auto oldBitmap = SelectObject(dc, bitmap);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, width, height, nullptr);
        SetViewportExtEx(dc, client.right, client.bottom, nullptr);
        auto fill = [&](RECT rect, COLORREF color) { HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush); };
        fill({0, 0, width, height}, RGB(24, 24, 22));
        // Brand background rendered directly into the native surface; no disk IO.
        const int visualHeight = height - 168;
        fill({0, 0, width, visualHeight}, RGB(36, 35, 32));
        const int saved = SaveDC(dc);
        // Keep the decorative texture behind the artwork, never behind status text.
        IntersectClipRect(dc, 335, 0, width, visualHeight);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 57, 51));
        auto oldPen = SelectObject(dc, pen); auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (int ring = 0; ring < 9; ++ring) {
            int radius = 52 + ring * 27;
            Ellipse(dc, width * 3 / 4 - radius, visualHeight / 2 - radius,
                width * 3 / 4 + radius, visualHeight / 2 + radius);
        }
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
        RestoreDC(dc, saved);
        SetBkMode(dc, TRANSPARENT);
        auto text = [&](const std::wstring& value, RECT rect, int size, COLORREF color, UINT flags = DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS) {
            HFONT font = CreateFontW(-size, 0, 0, 0, size > 30 ? FW_SEMIBOLD : FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            auto previous = SelectObject(dc, font); SetTextColor(dc, color);
            DrawTextW(dc, value.c_str(), -1, &rect, flags);
            SelectObject(dc, previous); DeleteObject(font);
        };
        const int logoPixels = std::max(72, MulDiv(72, client.right, width));
        HICON logo = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, logoPixels, logoPixels, LR_DEFAULTCOLOR));
        if (logo) {
            // The embedded brand icon is dark. Render a light silhouette on the
            // splash only, keeping the original application icon unchanged.
            HDC logoDC = CreateCompatibleDC(target);
            HBITMAP logoBitmap = CreateCompatibleBitmap(target, logoPixels, logoPixels);
            auto previous = SelectObject(logoDC, logoBitmap);
            PatBlt(logoDC, 0, 0, logoPixels, logoPixels, WHITENESS);
            DrawIconEx(logoDC, 0, 0, logo, logoPixels, logoPixels, 0, nullptr, DI_NORMAL);
            PatBlt(logoDC, 0, 0, logoPixels, logoPixels, DSTINVERT);
            StretchBlt(dc, 34, 60, 72, 72, logoDC, 0, 0, logoPixels, logoPixels, SRCPAINT);
            SelectObject(logoDC, previous); DeleteObject(logoBitmap); DeleteDC(logoDC); DestroyIcon(logo);
        }
        text(L"TomCat", {124, 63, width - 25, 125}, 48, RGB(243, 239, 229));
        text(L"CREATE YOUR WORLD", {38, 164, width - 25, 200}, 16, RGB(170, 166, 156));
        std::wstring version(Version::ProductVersion.begin(), Version::ProductVersion.end());
        text(L"EDITOR  " + version, {38, visualHeight - 41, width - 28, visualHeight - 10}, 14, RGB(170, 166, 156));
        const int barY = visualHeight;
        fill({0, barY, width, barY + 4}, RGB(60, 57, 51));
        if (!failed) {
            if (total) fill({0, barY, static_cast<LONG>(width * (static_cast<double>(processed) / total)), barY + 4}, RGB(243, 239, 229));
            else { int x = static_cast<int>((GetTickCount64() / 8) % (width + 160)) - 160;
                fill({std::max(0, x), barY, std::min(width, x + 160), barY + 4}, RGB(243, 239, 229)); }
        } else fill({0, barY, width, barY + 4}, RGB(217, 121, 107));
        text(stage, {38, barY + 28, width - (total && !failed ? 130 : 28), barY + 60}, 21, failed ? RGB(217, 121, 107) : RGB(243, 239, 229));
        if (failed) {
            text(detail, {32, barY + 52, width - 28, height - 42}, 14, RGB(224, 177, 168), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        } else {
            text(project, {38, barY + 68, width - (total ? 240 : 28), barY + 98}, 18, RGB(170, 166, 156));
            if (total) {
                const auto percent = std::to_wstring(static_cast<unsigned>(std::min(100.0, 100.0 * processed / total))) + L"%";
                text(percent, {width - 130, barY + 28, width - 38, barY + 60}, 24, RGB(243, 239, 229), DT_RIGHT | DT_SINGLELINE);
                const auto bytes = std::to_wstring(processed / (1024 * 1024)) + L" / " + std::to_wstring(total / (1024 * 1024)) + L" MB";
                text(bytes, {width - 240, barY + 68, width - 38, barY + 98}, 16, RGB(170, 166, 156), DT_RIGHT | DT_SINGLELINE);
            }
        }
        SetMapMode(dc, MM_TEXT);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); EndPaint(window, &paint);
    }
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::thread m_Thread;
    HWND m_Window = nullptr;
    bool m_Created = false, m_Error = false;
    uint64_t m_Processed = 0, m_Total = 0;
    std::wstring m_Stage = L"正在准备运行环境", m_Project, m_Detail;
    std::filesystem::path m_Log;
    HWND m_LogControl = nullptr, m_CloseControl = nullptr;
};
}
#else
namespace TomCat {
class EditorStartupWindow {
public:
    static EditorStartupWindow& Get() { static EditorStartupWindow instance; return instance; }
    template<class... T> void SetProject(T&&...) {}
    template<class... T> void Update(T&&...) {}
    void Close() {}
};
}
#endif
