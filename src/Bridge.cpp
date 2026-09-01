#include "Bridge.h"
#include "UiLayoutLoader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef MSG_HAVE_SODIUM
#include <sodium.h>
#endif

namespace msg {

using json = nlohmann::json;

Bridge::Bridge(NetworkClient& net) : net_(net) {}

// Reads the per-account salt file if it exists, otherwise generates one
// and writes it (salts aren't secret - they just need to be unique and
// stable across logins so the same password re-derives the same key).
// Lives in the account's own data directory so it travels naturally with
// LocalStore's file, and never touches the network.
static crypto::Bytes getOrCreateVaultSalt(const std::string& login) {
    auto saltPath = Config::instance().localDataDir() / login / "vault.salt";
    std::filesystem::create_directories(saltPath.parent_path());

    if (std::filesystem::exists(saltPath)) {
        std::ifstream in(saltPath, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (raw.size() == crypto::VAULT_SALT_BYTES) {
            return crypto::Bytes(raw.begin(), raw.end());
        }
        // Wrong size (corrupt/truncated) - fall through and regenerate.
        // Any vault encrypted under the old salt becomes unreadable; this
        // is the same failure mode as a lost password and is logged
        // loudly by LocalStore::load() rather than silently discarding data.
    }

    auto salt = crypto::generateVaultSalt();
    std::ofstream out(saltPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(salt.data()), static_cast<std::streamsize>(salt.size()));
    return salt;
}

json Bridge::dispatch(const std::string& call, const json& args) {
    if (call == "getLayout")          return handleGetLayout(args);
    if (call == "getCaptcha")         return handleGetCaptcha(args);
    if (call == "getConfig")          return handleGetConfig(args);
    if (call == "setInterfaceMode")   return handleSetInterfaceMode(args);
    if (call == "register")           return handleRegister(args);
    if (call == "login")              return handleLogin(args);
    if (call == "logout")             return handleLogout(args);
    if (call == "getSession")         return handleGetSession(args);
    if (call == "getConnectionStatus") return handleGetConnectionStatus(args);
    if (call == "listChats")          return handleListChats(args);
    if (call == "getMessages")        return handleGetMessages(args);
    if (call == "sendMessage")        return handleSendMessage(args);
    if (call == "createChat")         return handleCreateChat(args);
    if (call == "syncMessages")       return handleSyncMessages(args);
    if (call == "getVaultStatus")     return handleGetVaultStatus(args);

    return json{{"ok", false}, {"error", "unknown_call: " + call}};
}

json Bridge::handleGetLayout(const json& args) {
    std::string screen = args.value("screen", "");
    if (screen.empty()) return json{{"ok", false}, {"error", "missing_screen"}};

    auto path = std::filesystem::current_path() / "ui" / "shared" / (screen + ".layout.xml");
    auto layout = UiLayoutLoader::loadLayout(path);
    return json{{"ok", true}, {"layout", layout}};
}

json Bridge::handleGetCaptcha(const json&) {
    auto res = net_.getCaptcha();
    if (!res.ok) return json{{"ok", false}, {"error", res.error}};
    return json{{"ok", true}, {"id", res.data.value("id", "")}, {"question", res.data.value("question", "")}};
}

json Bridge::handleGetConfig(const json&) {
    auto& cfg = Config::instance();
    return json{
        {"ok", true},
        {"server", {{"host", cfg.server().host}, {"port", cfg.server().port}}},
        {"interfaceMode", cfg.interfaceMode()},
        {"limits", {
            {"freeMaxUploadGb", cfg.limits().free_max_upload_gb},
            {"premiumMaxUploadGb", cfg.limits().premium_max_upload_gb}
        }}
    };
}

json Bridge::handleSetInterfaceMode(const json& args) {
    std::string mode = args.value("mode", "standard");
    if (mode != "standard" && mode != "pro")
        return json{{"ok", false}, {"error", "invalid_mode"}};

    Config::instance().setInterfaceMode(mode);
    AppState::instance().setInterfaceMode(mode == "pro" ? InterfaceMode::Pro : InterfaceMode::Standard);

    // Actually switch what's on screen, not just persist a preference for
    // next launch - the toggle on the login screen and in Settings both
    // expect the window to visibly change right away.
    if (onNavigateRequest) {
        onNavigateRequest(mode == "pro" ? "ui/pro/index.html" : "ui/standard/index.html");
    }
    return json{{"ok", true}, {"mode", mode}};
}

json Bridge::handleRegister(const json& args) {
    auto res = net_.registerAccount(
        args.value("login", ""),
        args.value("displayName", ""),
        args.value("password", ""),
        args.value("captchaToken", "")
    );
    if (!res.ok) return json{{"ok", false}, {"error", res.error}};
    return json{{"ok", true}};
}

json Bridge::handleLogin(const json& args) {
    std::string login = args.value("login", "");
    std::string password = args.value("password", "");

    auto res = net_.login(login, password, args.value("captchaToken", ""));
    if (!res.ok) return json{{"ok", false}, {"error", res.error}};

    Session s;
    s.login = login;
    s.displayName = res.data.value("display_name", s.login);
    s.sessionToken = res.data.value("session_token", "");
    s.isPremium = res.data.value("is_premium", false);
    s.starsBalance = res.data.value("stars_balance", 0LL);

    // Derive the local vault key from the password RIGHT HERE, while we
    // still have it in plaintext for the auth call above - it is never
    // stored, never sent anywhere else, and this is the only place in the
    // whole client that touches it after this call returns.
    auto salt = getOrCreateVaultSalt(login);
    if (auto key = crypto::deriveVaultKey(password, salt)) {
        s.vaultKey = std::move(*key);
    } else {
        std::cerr << "[Bridge] vault key derivation failed for '" << login
                  << "' - local chat history will be unencrypted this session.\n";
    }
#ifdef MSG_HAVE_SODIUM
    sodium_memzero(password.data(), password.size());
#endif

    AppState::instance().setSession(s);
    store_.reset(); // force store() to rebuild with the fresh vault key below

    return json{{"ok", true}, {"displayName", s.displayName}, {"isPremium", s.isPremium}, {"starsBalance", s.starsBalance}};
}

json Bridge::handleLogout(const json&) {
    AppState::instance().clearSession();
    net_.disconnect();
    // Drop the decrypted-in-memory store now rather than waiting for the
    // next chat call to notice the login changed - no reason to keep this
    // account's plaintext chat data resident any longer than necessary.
    store_.reset();
    return json{{"ok", true}};
}

json Bridge::handleGetSession(const json&) {
    auto s = AppState::instance().currentSession();
    if (!s) return json{{"ok", true}, {"loggedIn", false}};
    return json{
        {"ok", true}, {"loggedIn", true},
        {"login", s->login}, {"displayName", s->displayName},
        {"isPremium", s->isPremium}, {"starsBalance", s->starsBalance}
    };
}

json Bridge::handleGetConnectionStatus(const json&) {
    // Drives the small corner "нет соединения" indicator. The client keeps
    // working from locally cached data regardless of this value - see
    // README "LocalVault" note; this call never blocks on the network.
    return json{{"ok", true}, {"online", net_.isReachable()}};
}

LocalStore& Bridge::store() {
    // Namespaced by login, not one shared file: a PC can have more than one
    // account logged in over time (shared machine, or just switching
    // accounts), and their local chat histories must not mix. Falls back to
    // "_guest" only if something calls a chat op before any session exists
    // (shouldn't happen from the UI, but better than crashing).
    auto session = AppState::instance().currentSession();
    std::string login = session ? session->login : "_guest";

    if (!store_ || store_->ownerLogin() != login) {
        auto dir = Config::instance().localDataDir() / login;
        crypto::Bytes vaultKey = session ? session->vaultKey : crypto::Bytes{};
        store_ = std::make_unique<LocalStore>(dir, login, vaultKey);
        store_->load();
    }
    return *store_;
}

json Bridge::handleListChats(const json&) {
    return json{{"ok", true}, {"chats", store().listChatsJson()}};
}

json Bridge::handleGetMessages(const json& args) {
    std::string chatId = args.value("chatId", "");
    if (chatId.empty()) return json{{"ok", false}, {"error", "missing_chatId"}};
    return json{{"ok", true}, {"messages", store().getMessagesJson(chatId)}};
}

json Bridge::handleSendMessage(const json& args) {
    std::string chatId = args.value("chatId", "");
    std::string text = args.value("text", "");
    if (chatId.empty() || text.empty()) return json{{"ok", false}, {"error", "missing_fields"}};

    auto session = AppState::instance().currentSession();
    std::string from = session ? session->login : "me";

    // Always write locally first - the sender's own copy must exist
    // regardless of whether the network leg succeeds, same as any
    // store-then-forward messenger.
    std::string id = store().appendMessage(chatId, from, text);
    if (id.empty()) return json{{"ok", false}, {"error", "chat_not_found"}};

    // Then relay to the recipient's account, if this is a dm with a real
    // peer login attached. Group/channel fan-out isn't built yet (see
    // README) - only 1:1 delivery works today.
    auto chat = store().findChatById(chatId);
    bool delivered = false;
    std::string deliveryError;
    if (chat && chat->type == "dm" && !chat->peerLogin.empty() && session) {
        auto res = net_.sendRemoteMessage(session->sessionToken, chat->peerLogin, text);
        delivered = res.ok;
        if (!res.ok) deliveryError = res.error;
    }

    return json{{"ok", true}, {"id", id}, {"delivered", delivered}, {"deliveryError", deliveryError}};
}

json Bridge::handleCreateChat(const json& args) {
    std::string type = args.value("type", "dm");
    std::string name = args.value("name", "");
    std::string peerLogin = args.value("peerLogin", "");
    if (name.empty()) return json{{"ok", false}, {"error", "missing_name"}};
    if (type != "dm" && type != "group" && type != "channel")
        return json{{"ok", false}, {"error", "invalid_type"}};
    if (type == "dm" && peerLogin.empty())
        return json{{"ok", false}, {"error", "missing_peer_login"}};

    if (type == "dm") {
        // Reuse the existing conversation instead of splitting history
        // across two chats with the same person.
        if (auto existing = store().findDmChatByPeerLogin(peerLogin)) {
            return json{{"ok", true}, {"id", *existing}, {"existing", true}};
        }
    }

    std::string id = store().createChat(type, name, peerLogin);
    return json{{"ok", true}, {"id", id}};
}

json Bridge::handleSyncMessages(const json&) {
    auto session = AppState::instance().currentSession();
    if (!session) return json{{"ok", false}, {"error", "not_logged_in"}};

    auto res = net_.pollMessages(session->sessionToken);
    if (!res.ok) return json{{"ok", false}, {"error", res.error}};

    int newCount = 0;
    for (const auto& m : res.data.value("messages", json::array())) {
        std::string fromLogin = m.value("from_login", "");
        std::string body = m.value("body", "");
        if (fromLogin.empty() || body.empty()) continue;

        std::string chatId;
        if (auto existing = store().findDmChatByPeerLogin(fromLogin)) {
            chatId = *existing;
        } else {
            // First message ever from this account - auto-create the dm
            // chat so it shows up in the sidebar without the user having
            // to pre-add them.
            chatId = store().createChat("dm", fromLogin, fromLogin);
        }
        store().appendMessage(chatId, fromLogin, body);
        ++newCount;
    }

    return json{{"ok", true}, {"newCount", newCount}};
}

json Bridge::handleGetVaultStatus(const json&) {
    return json{{"ok", true}, {"encrypted", store().isEncrypted()}};
}

} // namespace msg
