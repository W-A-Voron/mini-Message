#include "WebViewHost.h"
#include "Branding.h"
#include <iostream>
#include <filesystem>

#ifdef MSG_USE_WEBVIEW2
  #include <windows.h>
  #include <wrl.h>
  #include "WebView2.h"
  #include "TrayIcon.h"
  using Microsoft::WRL::ComPtr;
  using Microsoft::WRL::Callback;
#endif

namespace msg {

#ifdef MSG_USE_WEBVIEW2

// --- small UTF-8 <-> UTF-16 helpers (avoid deprecated <codecvt>) ---------
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

struct WebViewHost::Impl {
    HWND hwnd = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    Bridge* bridge = nullptr;
    HICON appIcon = nullptr;
    TrayIcon tray;
    bool exiting = false; // true once the user picked "Выход", so WM_CLOSE really closes
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<WebViewHost::Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_SIZE:
            if (impl && impl->controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                impl->controller->put_Bounds(bounds);
            }
            return 0;

        case WM_APP_TRAY: {
            if (!impl) break;
            bool restore = false, exit = false;
            if (impl->tray.handleTrayMessage(wp, lp, restore, exit)) {
                if (restore) {
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
                if (exit) {
                    impl->exiting = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            // "Свернуть в трей" instead of quitting - matches how Telegram
            // Desktop behaves by default. Real exit only happens via the
            // tray menu's "Выход".
            if (impl && !impl->exiting) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            return DefWindowProc(hwnd, msg, wp, lp);

        case WM_DESTROY:
            if (impl) impl->tray.destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

WebViewHost::WebViewHost(Bridge& bridge) : bridge_(bridge) {
    impl_ = new Impl();
    impl_->bridge = &bridge;
}

WebViewHost::~WebViewHost() {
    if (impl_ && impl_->appIcon) DestroyIcon(impl_->appIcon);
    delete impl_;
}

static HICON loadAppIcon() {
    // Expects an .ico next to the exe at image/messenger.ico (copied there
    // by the CMake POST_BUILD step from src/image/ - PNG must be converted
    // to .ico first; LoadImage doesn't decode PNG directly on old Windows).
    std::filesystem::path iconPath = std::filesystem::current_path() / "image" / "messenger.ico";
    if (!std::filesystem::exists(iconPath)) return nullptr;
    return (HICON)LoadImageW(nullptr, iconPath.wstring().c_str(), IMAGE_ICON,
                              0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

bool WebViewHost::createAndShow(const std::string& entryHtmlRelativePath, int width, int height) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    impl_->appIcon = loadAppIcon();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MiniMessageWindow";
    wc.hIcon = impl_->appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    impl_->hwnd = CreateWindowW(wc.lpszClassName, branding::kAppName,
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                width, height, nullptr, nullptr, hInst, nullptr);
    if (!impl_->hwnd) return false;
    SetWindowLongPtr(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_));

    if (impl_->appIcon) {
        SendMessageW(impl_->hwnd, WM_SETICON, ICON_BIG, (LPARAM)impl_->appIcon);
        SendMessageW(impl_->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)impl_->appIcon);
    }

    impl_->tray.create(impl_->hwnd, impl_->appIcon, branding::kAppName);
    ShowWindow(impl_->hwnd, SW_SHOW);

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, entryHtmlRelativePath](HRESULT envResult, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(envResult) || !env) {
                    std::cerr << "[WebViewHost] Failed to create WebView2 environment (hr=0x"
                              << std::hex << envResult << "). Is the WebView2 Runtime installed?\n";
                    return envResult;
                }
                env->CreateCoreWebView2Controller(
                    impl_->hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, entryHtmlRelativePath](HRESULT ctrlResult, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(ctrlResult) || !ctrl) {
                                std::cerr << "[WebViewHost] Failed to create WebView2 controller (hr=0x"
                                          << std::hex << ctrlResult << ").\n";
                                return ctrlResult;
                            }
                            impl_->controller = ctrl;
                            impl_->controller->get_CoreWebView2(&impl_->webview);

                            RECT bounds;
                            GetClientRect(impl_->hwnd, &bounds);
                            impl_->controller->put_Bounds(bounds);

                            // --- THE FIX -------------------------------------------------
                            // This handler was missing in the last edit, which is why the
                            // window rendered as a blank colored rectangle: JS's
                            // postMessage() calls (getLayout, login, register, ...) had
                            // nobody to answer them, so every Promise in app.js just hung
                            // forever and #app never got populated.
                            EventRegistrationToken token;
                            impl_->webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        // IMPORTANT: bridge.js posts a JS OBJECT
                                        // ({call, args, requestId}), not a plain
                                        // string. TryGetWebMessageAsString only
                                        // succeeds for messages posted as an
                                        // actual string - for an object it fails,
                                        // and we used to just silently `return
                                        // S_OK` on that failure, meaning JS never
                                        // got a reply and every callNative() call
                                        // timed out after 10s (this is what the
                                        // "native_timeout" / blank screen was).
                                        // get_WebMessageAsJson always returns the
                                        // JSON text regardless of the original
                                        // JS type, which is what we actually want.
                                        LPWSTR raw = nullptr;
                                        if (FAILED(args->get_WebMessageAsJson(&raw)) || !raw) {
                                            std::cerr << "[WebViewHost] get_WebMessageAsJson failed\n";
                                            return S_OK;
                                        }
                                        std::string utf8 = toUtf8(raw);
                                        CoTaskMemFree(raw);

                                        nlohmann::json envelope;
                                        try {
                                            envelope = nlohmann::json::parse(utf8);
                                        } catch (const std::exception& e) {
                                            std::cerr << "[WebViewHost] bad JS message: " << e.what() << "\n";
                                            return S_OK;
                                        }

                                        std::string call = envelope.value("call", "");
                                        auto args_ = envelope.value("args", nlohmann::json::object());
                                        long long requestId = envelope.value("requestId", 0LL);

                                        nlohmann::json result = impl_->bridge->dispatch(call, args_);

                                        nlohmann::json response = {
                                            {"requestId", requestId},
                                            {"result", result},
                                        };
                                        std::wstring wresponse = toWide(response.dump());
                                        sender->PostWebMessageAsJson(wresponse.c_str());
                                        return S_OK;
                                    }).Get(), &token);

                            navigateTo(entryHtmlRelativePath);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    return SUCCEEDED(hr);
}

void WebViewHost::navigateTo(const std::string& entryHtmlRelativePath) {
    if (!impl_->webview) return;

    std::filesystem::path basePath = std::filesystem::current_path();
    std::filesystem::path fullPath = basePath / entryHtmlRelativePath;

    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "[WebViewHost] File not found: " << fullPath << "\n";
        return;
    }

    std::wstring url = L"file:///" + fullPath.wstring();
    impl_->webview->Navigate(url.c_str());
}

int WebViewHost::runMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

#else // !MSG_USE_WEBVIEW2 -------------------------------------------------

struct WebViewHost::Impl {};

WebViewHost::WebViewHost(Bridge& bridge) : bridge_(bridge) {}
WebViewHost::~WebViewHost() = default;

bool WebViewHost::createAndShow(const std::string& entryHtmlRelativePath, int, int) {
    std::cerr << "[WebViewHost] Built without WebView2 - cannot render UI on this platform yet.\n"
              << "Would have loaded: " << entryHtmlRelativePath << "\n";
    return false;
}

void WebViewHost::navigateTo(const std::string&) {}
int WebViewHost::runMessageLoop() { return 0; }

#endif

} // namespace msg
