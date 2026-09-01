#include "LocalStore.h"
#include <fstream>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace msg {

using json = nlohmann::json;

static std::string randomId() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(rng);
    return oss.str();
}

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

LocalStore::LocalStore(std::filesystem::path dataDir, std::string ownerLogin, crypto::Bytes vaultKey)
    : ownerLogin_(std::move(ownerLogin)), vaultKey_(std::move(vaultKey)) {
    std::filesystem::create_directories(dataDir);
    filePath_ = dataDir / "messages.local.db";
    if (vaultKey_.empty()) {
        std::cerr << "[LocalStore] WARNING: no vault key for '" << ownerLogin_
                  << "' - chat history for this account will be written UNENCRYPTED to "
                  << filePath_ << "\n";
    }
}

// AAD binds ciphertext to which account it belongs to, so copying
// alice's vault file into bob's data directory fails to decrypt (wrong
// key AND wrong AAD) instead of silently decrypting under the wrong
// identity if the keys ever collided.
static crypto::Bytes vaultAad(const std::string& ownerLogin) {
    return crypto::Bytes(ownerLogin.begin(), ownerLogin.end());
}

bool LocalStore::load() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::ifstream in(filePath_, std::ios::binary);
    if (!in.is_open()) {
        // First run - not an error, just nothing saved yet.
        chats_.clear();
        return true;
    }

    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    json j;
    if (!vaultKey_.empty()) {
        crypto::Bytes ciphertext(raw.begin(), raw.end());
        auto plaintext = crypto::decryptMessage(vaultKey_, ciphertext, vaultAad(ownerLogin_));
        if (!plaintext) {
            std::cerr << "[LocalStore] decrypt failed for '" << ownerLogin_ << "' - wrong password-"
                         "derived key, or the file is corrupt/tampered. Refusing to overwrite it; "
                         "leaving in-memory chat list empty for this session.\n";
            return false;
        }
        try {
            j = json::parse(std::string(plaintext->begin(), plaintext->end()));
        } catch (const std::exception&) {
            std::cerr << "[LocalStore] decrypted content isn't valid JSON for '" << ownerLogin_ << "'.\n";
            return false;
        }
    } else {
        try {
            j = json::parse(raw);
        } catch (const std::exception&) {
            return false; // corrupt file - leave chats_ as-is rather than wipe it
        }
    }

    chats_.clear();
    for (const auto& c : j.value("chats", json::array())) {
        Chat chat;
        chat.id = c.value("id", "");
        chat.type = c.value("type", "dm");
        chat.name = c.value("name", "");
        chat.peerLogin = c.value("peerLogin", "");
        for (const auto& m : c.value("messages", json::array())) {
            ChatMessage msg;
            msg.id = m.value("id", "");
            msg.from = m.value("from", "");
            msg.text = m.value("text", "");
            msg.timestampMs = m.value("ts", 0LL);
            chat.messages.push_back(std::move(msg));
        }
        chats_.push_back(std::move(chat));
    }
    return true;
}

bool LocalStore::save() const {
    std::lock_guard<std::mutex> lock(mtx_);
    json j;
    j["chats"] = json::array();
    for (const auto& c : chats_) {
        json jc;
        jc["id"] = c.id;
        jc["type"] = c.type;
        jc["name"] = c.name;
        jc["peerLogin"] = c.peerLogin;
        jc["messages"] = json::array();
        for (const auto& m : c.messages) {
            jc["messages"].push_back({{"id", m.id}, {"from", m.from}, {"text", m.text}, {"ts", m.timestampMs}});
        }
        j["chats"].push_back(std::move(jc));
    }

    std::string dumped = j.dump();

    std::ofstream out(filePath_, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    if (!vaultKey_.empty()) {
        crypto::Bytes plaintext(dumped.begin(), dumped.end());
        crypto::Bytes ciphertext;
        try {
            ciphertext = crypto::encryptMessage(vaultKey_, plaintext, vaultAad(ownerLogin_));
        } catch (const std::exception& e) {
            std::cerr << "[LocalStore] encrypt failed for '" << ownerLogin_ << "': " << e.what() << "\n";
            return false;
        }
        out.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));
    } else {
        out << dumped;
    }
    return true;
}

json LocalStore::listChatsJson() const {
    std::lock_guard<std::mutex> lock(mtx_);
    json arr = json::array();
    for (const auto& c : chats_) {
        const ChatMessage* last = c.messages.empty() ? nullptr : &c.messages.back();
        arr.push_back({
            {"id", c.id},
            {"type", c.type},
            {"name", c.name},
            {"peerLogin", c.peerLogin},
            {"lastMessage", last ? last->text : ""},
            {"lastTimestampMs", last ? last->timestampMs : 0},
            {"messageCount", c.messages.size()},
        });
    }
    return arr;
}

json LocalStore::getMessagesJson(const std::string& chatId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& c : chats_) {
        if (c.id != chatId) continue;
        json arr = json::array();
        for (const auto& m : c.messages) {
            arr.push_back({{"id", m.id}, {"from", m.from}, {"text", m.text}, {"ts", m.timestampMs}});
        }
        return arr;
    }
    return json::array();
}

std::optional<Chat> LocalStore::findChatById(const std::string& chatId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& c : chats_) {
        if (c.id == chatId) return c;
    }
    return std::nullopt;
}

std::string LocalStore::appendMessage(const std::string& chatId, const std::string& from, const std::string& text) {
    std::string newId;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& c : chats_) {
            if (c.id != chatId) continue;
            ChatMessage m;
            m.id = randomId();
            m.from = from;
            m.text = text;
            m.timestampMs = nowMs();
            c.messages.push_back(m);
            newId = m.id;
            found = true;
            break;
        }
    } // lock released here, before save() re-locks
    if (found) save();
    return newId;
}

std::string LocalStore::createChat(const std::string& type, const std::string& name, const std::string& peerLogin) {
    Chat c;
    c.id = randomId();
    c.type = type;
    c.name = name;
    c.peerLogin = peerLogin;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        chats_.push_back(c);
    }
    save();
    return c.id;
}

std::optional<std::string> LocalStore::findDmChatByPeerLogin(const std::string& peerLogin) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& c : chats_) {
        if (c.type == "dm" && c.peerLogin == peerLogin) return c.id;
    }
    return std::nullopt;
}

} // namespace msg
