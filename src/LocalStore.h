#pragma once
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "Crypto.h"

namespace msg {

// The client spec calls for "все сообщения хранятся локально на ПК с
// клиентом" - a per-account JSON file on disk under Config::localDataDir(),
// holding every chat/channel/group and every message. As of this pass it
// is genuinely ENCRYPTED AT REST: the file content is AEAD-encrypted
// (XChaCha20-Poly1305, via Crypto.h) with a key derived from the account's
// login password via Argon2id (crypto::deriveVaultKey) - the same KDF
// family the server uses for password hashing, not a home-grown scheme.
// The password itself is never written to disk; only a per-account random
// salt is (see vaultSaltPath in LocalStore.cpp) - salts aren't secret,
// they just need to be unique, which is what lets the same password
// re-derive the same key on the next login without storing the key itself.
//
// Honest scope note: this protects data AT REST on this machine (someone
// copying the file off your disk, or another OS user account, can't read
// it without your password). It does NOT protect messages IN TRANSIT
// between two accounts - MessagingService on the server still relays
// plaintext bodies today; wiring the Double-Ratchet primitives in Crypto.h
// through that pipe is a separate, still-open piece of work.
struct ChatMessage {
    std::string id;
    std::string from;   // login of the sender ("me" for locally-authored)
    std::string text;
    int64_t timestampMs = 0;
};

struct Chat {
    std::string id;
    std::string type;   // "dm" | "group" | "channel"
    std::string name;
    std::string peerLogin; // "dm" only: the actual account this chat delivers to/from
    std::vector<ChatMessage> messages;
};

class LocalStore {
public:
    // `vaultKey` must be crypto::VAULT_KEY_BYTES long (derived via
    // crypto::deriveVaultKey - see Bridge::store() for where that
    // happens, right after a successful login). An empty key means
    // "encryption unavailable" (e.g. built without libsodium) - the
    // store still works but the file is written in the clear, and
    // load()/save() log a warning every time so this never fails silently.
    LocalStore(std::filesystem::path dataDir, std::string ownerLogin, crypto::Bytes vaultKey);

    bool load();
    bool save() const;

    const std::string& ownerLogin() const { return ownerLogin_; }
    bool isEncrypted() const { return !vaultKey_.empty(); }

    nlohmann::json listChatsJson() const;                     // summaries only (no message bodies)
    nlohmann::json getMessagesJson(const std::string& chatId) const;
    std::optional<Chat> findChatById(const std::string& chatId) const;

    // Returns the new message's id, or empty string if chatId doesn't exist.
    std::string appendMessage(const std::string& chatId, const std::string& from, const std::string& text);

    // Returns the new chat's id. peerLogin only meaningful/used for type=="dm".
    std::string createChat(const std::string& type, const std::string& name, const std::string& peerLogin = "");

    // Finds the dm chat delivering to/from this login, if one already
    // exists. Used when an incoming message arrives (see Bridge::
    // handleSyncMessages) so replies from someone you haven't explicitly
    // "created a chat with" still land in one continuous conversation
    // instead of a new chat per message.
    std::optional<std::string> findDmChatByPeerLogin(const std::string& peerLogin) const;

private:
    mutable std::mutex mtx_;
    std::filesystem::path filePath_;
    std::string ownerLogin_;
    crypto::Bytes vaultKey_;
    std::vector<Chat> chats_;
};

} // namespace msg
