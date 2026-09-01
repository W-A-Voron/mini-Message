#pragma once
#include <string>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "Config.h"
#include "AppState.h"
#include "NetworkClient.h"
#include "LocalStore.h"

namespace msg {

// Every message coming from JS (via window.chrome.webview.postMessage / the
// WebView2 host callback) is a JSON envelope: {"call": "...", "args": {...}}.
// Bridge dispatches it to the right handler and returns a JSON result that
// WebViewHost posts back to JS as a resolved/rejected Promise.
//
// This keeps ALL business logic (auth, crypto, storage, limits) in C++;
// the web layer is presentation + input only, per the spec's "логика
// клиента на C++" requirement.
class Bridge {
public:
    explicit Bridge(NetworkClient& net);

    // Set by main.cpp after constructing WebViewHost, so the "переключить
    // интерфейс" action (login screen toggle, Settings) can make the
    // window actually navigate, not just persist a preference for next
    // launch. Optional on purpose: Bridge must still work (e.g. --cli mode,
    // tests) with no window attached at all.
    std::function<void(const std::string& entryHtmlRelativePath)> onNavigateRequest;

    // Returns a JSON response for a given JS call. Thread: called on the
    // UI thread by WebViewHost; handlers that block on network should hop
    // to a worker thread in a later pass (kept synchronous here for clarity).
    nlohmann::json dispatch(const std::string& call, const nlohmann::json& args);

private:
    nlohmann::json handleGetLayout(const nlohmann::json& args);
    nlohmann::json handleGetCaptcha(const nlohmann::json& args);
    nlohmann::json handleGetConfig(const nlohmann::json& args);
    nlohmann::json handleSetInterfaceMode(const nlohmann::json& args);
    nlohmann::json handleRegister(const nlohmann::json& args);
    nlohmann::json handleLogin(const nlohmann::json& args);
    nlohmann::json handleLogout(const nlohmann::json& args);
    nlohmann::json handleGetSession(const nlohmann::json& args);
    nlohmann::json handleGetConnectionStatus(const nlohmann::json& args);

    // Chats/channels/groups - local-only for now (see LocalStore.h for what
    // that does and doesn't mean yet).
    nlohmann::json handleListChats(const nlohmann::json& args);
    nlohmann::json handleGetMessages(const nlohmann::json& args);
    nlohmann::json handleSendMessage(const nlohmann::json& args);
    nlohmann::json handleCreateChat(const nlohmann::json& args);
    // Pulls anything queued for this account on the server and merges it
    // into LocalStore (creating a dm chat for new senders automatically).
    nlohmann::json handleSyncMessages(const nlohmann::json& args);
    nlohmann::json handleGetVaultStatus(const nlohmann::json& args);

    NetworkClient& net_;
    std::unique_ptr<LocalStore> store_; // lazily created on first chat call, after Config is loaded
    LocalStore& store();
};

} // namespace msg

