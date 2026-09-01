#include "AppState.h"
#ifdef MSG_HAVE_SODIUM
#include <sodium.h>
#endif

namespace msg {

AppState& AppState::instance() {
    static AppState s;
    return s;
}

std::optional<Session> AppState::currentSession() {
    std::lock_guard<std::mutex> lock(mtx_);
    return session_;
}

void AppState::setSession(Session s) {
    std::lock_guard<std::mutex> lock(mtx_);
    session_ = std::move(s);
}

void AppState::clearSession() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (session_ && !session_->vaultKey.empty()) {
#ifdef MSG_HAVE_SODIUM
        // Best-effort: zero the vault key's bytes before the vector is
        // destroyed, instead of just letting it become unreachable memory
        // that std::vector's deallocation doesn't guarantee is wiped.
        sodium_memzero(session_->vaultKey.data(), session_->vaultKey.size());
#endif
    }
    session_.reset();
}

} // namespace msg
