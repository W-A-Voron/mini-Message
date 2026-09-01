#pragma once
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <mutex>

namespace msg {

enum class InterfaceMode { Standard, Pro };

struct Session {
    std::string login;
    std::string displayName;
    std::string sessionToken;  // opaque token issued by server after auth
    bool isPremium = false;
    long long starsBalance = 0;
    std::vector<uint8_t> vaultKey; // derived from password at login; see Crypto::deriveVaultKey. Never persisted.
};

// Everything the UI (via Bridge) needs to read/mutate at runtime.
// Single mutex for now - this is a thin client, not a hot path.
class AppState {
public:
    static AppState& instance();

    std::optional<Session> currentSession();
    void setSession(Session s);
    void clearSession();

    InterfaceMode interfaceMode() const { return mode_; }
    void setInterfaceMode(InterfaceMode m) { mode_ = m; }

private:
    AppState() = default;
    mutable std::mutex mtx_;
    std::optional<Session> session_;
    InterfaceMode mode_ = InterfaceMode::Standard;
};

} // namespace msg
