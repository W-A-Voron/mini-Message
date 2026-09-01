#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace msg {

// Talks to the server for:
//   - registration / login (captcha + login + password)
//   - premium / stars / gifts state sync
//   - paid group & channel settings
//   - message relay (send_message / poll_messages) - see MessagingService
//     on the server: it's a transient pickup queue, not permanent storage,
//     and bodies travel as plaintext for now (E2E via Crypto.h isn't wired
//     to this pipe yet - an honest gap, not a secret one).
//
// NOTE: This first pass implements the request/response shape and a plain
// TCP transport so the rest of the client (Bridge, UI) can be built and
// tested against a fake/local server. Swapping the transport for TLS
// (OpenSSL/Schannel) is isolated to connect()/sendRaw()/recvRaw().
class NetworkClient {
public:
    explicit NetworkClient(std::string host, uint16_t port, bool useTls);
    ~NetworkClient();

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // Each call is a simple JSON request -> JSON response round trip.
    // {"op": "...", ...fields} -> {"ok": true/false, ...fields}
    struct Result {
        bool ok = false;
        nlohmann::json data;
        std::string error;
    };

    Result registerAccount(const std::string& login,
                            const std::string& displayName,
                            const std::string& password,
                            const std::string& captchaToken);

    Result login(const std::string& login,
                 const std::string& password,
                 const std::string& captchaToken);

    Result fetchAccountState(const std::string& sessionToken);

    // Sends one message to another account by login. Delivery, not
    // storage: the server holds it only until the recipient polls.
    Result sendRemoteMessage(const std::string& sessionToken,
                              const std::string& toLogin,
                              const std::string& body);

    // Fetches (and consumes, server-side) everything queued for this
    // account since the last poll. {"ok":true,"messages":[{from_login,body,ts}, ...]}
    Result pollMessages(const std::string& sessionToken);

    // Server-issued math captcha (see server's CaptchaService). Returns
    // {ok, id, question}. UI shows `question`, user types the answer, and
    // the pair travels back as captchaToken = "<id>|<answer>" to
    // register()/login() below - keeps the wire format simple without a
    // separate field threaded through every auth call.
    Result getCaptcha();

    // Reconfigure target at runtime (Settings screen / --server CLI flag).
    void setEndpoint(std::string host, uint16_t port);

    // --- Offline-mode support -------------------------------------------
    // Spawns a background thread that periodically tries to reach the
    // server. Does NOT block the caller and does NOT throw on failure -
    // the whole point is the app stays usable (reading local data) while
    // this silently retries. Bridge::handleGetConnectionStatus() reads
    // isReachable() to drive the small "нет соединения" corner indicator.
    void startConnectionMonitor(int intervalSeconds = 5);
    void stopConnectionMonitor();
    bool isReachable() const { return reachable_.load(); }

private:
    Result request(const nlohmann::json& payload);
    bool sendRaw(const std::string& data);
    bool recvRaw(std::string& out);

    std::string host_;
    uint16_t port_;
    bool useTls_;
    bool connected_ = false;
    int sockfd_ = -1; // platform socket handle; see NetworkClient.cpp

    std::atomic<bool> reachable_{false};
    std::atomic<bool> monitorRunning_{false};
    std::thread monitorThread_;
};

} // namespace msg
