#pragma once

// Windows-only system tray icon for Mini Message.
// Owned by WebViewHost::Impl; handles the WM_APP_TRAY callback message and
// the tray context menu (Открыть / Выход). Kept as a small standalone
// class so WebViewHost.cpp doesn't get any more crowded than it has to.

#ifdef MSG_USE_WEBVIEW2
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace msg {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT ID_TRAY_ICON = 1;
constexpr UINT ID_TRAY_MENU_OPEN = 1001;
constexpr UINT ID_TRAY_MENU_EXIT = 1002;

class TrayIcon {
public:
    // hIcon ownership stays with the caller (WebViewHost loads it once and
    // reuses it for both the window titlebar and the tray).
    void create(HWND hwnd, HICON hIcon, const std::wstring& tooltip) {
        hwnd_ = hwnd;
        ZeroMemory(&nid_, sizeof(nid_));
        nid_.cbSize = sizeof(NOTIFYICONDATAW);
        nid_.hWnd = hwnd;
        nid_.uID = ID_TRAY_ICON;
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid_.uCallbackMessage = WM_APP_TRAY;
        nid_.hIcon = hIcon;
        wcsncpy_s(nid_.szTip, tooltip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &nid_);
    }

    void destroy() {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }

    // Call from WndProc on WM_APP_TRAY. Returns true if it handled the
    // message (i.e. the caller doesn't need to do anything else with it).
    bool handleTrayMessage(WPARAM /*wp*/, LPARAM lp, bool& outRestoreWindow, bool& outExitRequested) {
        outRestoreWindow = false;
        outExitRequested = false;

        switch (lp) {
            case WM_LBUTTONDBLCLK:
                outRestoreWindow = true;
                return true;
            case WM_RBUTTONUP: {
                POINT pt;
                GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, ID_TRAY_MENU_OPEN, L"Открыть Mini Message");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, ID_TRAY_MENU_EXIT, L"Выход");

                // Required dance so the popup menu closes properly when
                // the user clicks elsewhere.
                SetForegroundWindow(hwnd_);
                UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                           pt.x, pt.y, 0, hwnd_, nullptr);
                DestroyMenu(menu);

                if (cmd == ID_TRAY_MENU_OPEN) outRestoreWindow = true;
                if (cmd == ID_TRAY_MENU_EXIT) outExitRequested = true;
                return true;
            }
            default:
                return false;
        }
    }

private:
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
};

} // namespace msg

#endif // MSG_USE_WEBVIEW2
