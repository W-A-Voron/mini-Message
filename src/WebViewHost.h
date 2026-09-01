#pragma once
#include <string>
#include <functional>
#include "Bridge.h"

namespace msg {

// Owns the native window + WebView2 control, loads ui/standard or ui/pro's
// index.html, and pipes window.chrome.webview.postMessage(...) calls from
// JS into Bridge::dispatch(), posting the JSON result back.
//
// This header intentionally exposes only lifecycle + the bridge wiring;
// all WebView2-specific COM plumbing lives in WebViewHost.cpp behind
// #ifdef MSG_USE_WEBVIEW2 so the rest of the client builds on any platform.
class WebViewHost {
public:
    explicit WebViewHost(Bridge& bridge);
    ~WebViewHost();

    // Creates the native window + WebView2 control and navigates to the
    // given entry HTML (e.g. "ui/standard/index.html" or "ui/pro/index.html").
    bool createAndShow(const std::string& entryHtmlRelativePath, int width, int height);

    // Blocks running the native message loop until the window closes.
    int runMessageLoop();

    // Navigate to a different entry point at runtime (e.g. switching
    // Standard <-> Pro interface from Settings without restarting).
    void navigateTo(const std::string& entryHtmlRelativePath);

    // Forward-declared here (defined in WebViewHost.cpp) so it stays public
    // as a *type* - WndProc and other free functions in the .cpp need to
    // name "WebViewHost::Impl*" for the GetWindowLongPtr/SetWindowLongPtr
    // round trip. The struct's actual fields are still only visible inside
    // WebViewHost.cpp, where Impl is defined - this doesn't leak WebView2/
    // COM types into this header, it just un-hides the type name itself.
    struct Impl;

private:
    Bridge& bridge_;
    Impl* impl_ = nullptr; // pimpl
};

} // namespace msg
