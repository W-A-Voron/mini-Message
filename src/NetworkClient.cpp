#include "NetworkClient.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

#include <cstring>
#include <iostream>
#include <chrono>

namespace msg {

// Surfaces the actual OS socket error instead of a bare true/false, so a
// send()/recv() failure right after a successful connect() (the classic
// "AV/firewall let the handshake through, then killed the connection"
// signature) shows up as an actual, look-up-able error code in the UI
// instead of a generic "send_failed" that hides what really happened.
static int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

NetworkClient::NetworkClient(std::string host, uint16_t port, bool useTls)
    : host_(std::move(host)), port_(port), useTls_(useTls) {}

NetworkClient::~NetworkClient() {
    stopConnectionMonitor();
    disconnect();
}

void NetworkClient::setEndpoint(std::string host, uint16_t port) {
    bool wasMonitoring = monitorRunning_.load();
    if (wasMonitoring) stopConnectionMonitor();
    if (connected_) disconnect();
    host_ = std::move(host);
    port_ = port;
    if (wasMonitoring) startConnectionMonitor();
}

void NetworkClient::startConnectionMonitor(int intervalSeconds) {
    if (monitorRunning_.exchange(true)) return; // already running
    monitorThread_ = std::thread([this, intervalSeconds] {
        while (monitorRunning_.load()) {
            // A short-lived probe connection, separate from the "real"
            // request socket, so an in-flight request isn't disturbed.
            NetworkClient probe(host_, port_, false);
            bool ok = probe.connect();
            if (ok) probe.disconnect();
            reachable_.store(ok);

            for (int i = 0; i < intervalSeconds * 10 && monitorRunning_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void NetworkClient::stopConnectionMonitor() {
    if (!monitorRunning_.exchange(false)) return;
    if (monitorThread_.joinable()) monitorThread_.join();
}

bool NetworkClient::connect() {
#ifdef _WIN32
    static bool wsaInit = false;
    if (!wsaInit) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        wsaInit = true;
    }
#endif

    sockfd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (sockfd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        // TODO: resolve hostnames, not just literal IPs, once DNS is needed.
        return false;
    }

    // Bounded connect: Bridge::dispatch() runs synchronously on the UI
    // thread (called straight from the WebView2 message handler), so a
    // blocking connect() with the OS default timeout (20s+ on Windows)
    // would freeze the whole window - no repaint, no message pump - the
    // moment the server is unreachable. That directly breaks the "stay
    // usable offline, just show a small corner warning" requirement.
    // Fix: non-blocking connect + select() with a short, fixed timeout.
    constexpr int kConnectTimeoutMs = 3000;

#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(sockfd_, FIONBIO, &nonBlocking);
#else
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
#endif

    int rc = ::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool connectOk = false;
    if (rc == 0) {
        connectOk = true; // connected immediately (e.g. localhost)
    } else {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sockfd_, &writeSet);
        timeval tv{ kConnectTimeoutMs / 1000, (kConnectTimeoutMs % 1000) * 1000 };

        if (select(static_cast<int>(sockfd_) + 1, nullptr, &writeSet, nullptr, &tv) > 0) {
            int soError = 0;
            socklen_t len = sizeof(soError);
            getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
            connectOk = (soError == 0);
        }
        // else: timed out or select() error - connectOk stays false
    }

#ifdef _WIN32
    u_long blocking = 0;
    ioctlsocket(sockfd_, FIONBIO, &blocking);
#else
    fcntl(sockfd_, F_SETFL, flags);
#endif

    if (!connectOk) {
#ifdef _WIN32
        closesocket(sockfd_);
#else
        ::close(sockfd_);
#endif
        sockfd_ = -1;
        return false;
    }

    if (useTls_) {
        // TODO: wrap sockfd_ with an OpenSSL BIO/SSL* handshake here.
        // Left unimplemented in this scaffold - do NOT ship without it.
        std::cerr << "[NetworkClient] WARNING: TLS requested but not yet wired up; "
                     "traffic is currently plaintext TCP.\n";
    }

    connected_ = true;
    return true;
}

void NetworkClient::disconnect() {
    if (sockfd_ >= 0) {
#ifdef _WIN32
        closesocket(sockfd_);
#else
        ::close(sockfd_);
#endif
        sockfd_ = -1;
    }
    connected_ = false;
}

bool NetworkClient::sendRaw(const std::string& data) {
    if (!connected_) return false;
    // Length-prefixed framing: 4-byte big-endian length + payload.
    uint32_t len = static_cast<uint32_t>(data.size());
    uint32_t netLen = htonl(len);
    if (send(sockfd_, reinterpret_cast<const char*>(&netLen), sizeof(netLen), 0) != sizeof(netLen))
        return false;
    return send(sockfd_, data.data(), static_cast<int>(data.size()), 0) == static_cast<int>(data.size());
}

bool NetworkClient::recvRaw(std::string& out) {
    if (!connected_) return false;
    uint32_t netLen = 0;
    if (recv(sockfd_, reinterpret_cast<char*>(&netLen), sizeof(netLen), MSG_WAITALL) != sizeof(netLen))
        return false;
    uint32_t len = ntohl(netLen);
    out.resize(len);
    return recv(sockfd_, out.data(), static_cast<int>(len), MSG_WAITALL) == static_cast<int>(len);
}

NetworkClient::Result NetworkClient::request(const nlohmann::json& payload) {
    Result r;
    if (!connected_ && !connect()) {
        r.error = "not_connected";
        return r;
    }

    std::string body = payload.dump();

    // The client holds one long-lived connection per app session, reused
    // across auth calls (get_captcha, then register/login whenever the
    // person finishes the form - which can be minutes later). Any idle
    // connection can die in that gap (server-side timeout, restart, a
    // flaky network) with no warning to us. Rather than surface that as a
    // hard error on whatever the user happened to be doing, retry once
    // with a fresh connection before giving up - this is exactly the
    // "send_failed right after a captcha that took a while" case.
    if (!sendRaw(body)) {
        int firstErr = lastSocketError();
        disconnect();
        if (!connect() || !sendRaw(body)) {
            int secondErr = lastSocketError();
            r.error = "send_failed:" + std::to_string(firstErr) + "/" + std::to_string(secondErr);
            return r;
        }
    }

    std::string respRaw;
    if (!recvRaw(respRaw)) {
        r.error = "recv_failed";
        return r;
    }
    try {
        auto j = nlohmann::json::parse(respRaw);
        r.ok = j.value("ok", false);
        r.data = j;
        if (!r.ok) r.error = j.value("error", "unknown_error");
    } catch (const std::exception& e) {
        r.error = std::string("bad_response: ") + e.what();
    }
    return r;
}

NetworkClient::Result NetworkClient::registerAccount(const std::string& login,
                                                       const std::string& displayName,
                                                       const std::string& password,
                                                       const std::string& captchaToken) {
    return request({
        {"op", "register"},
        {"login", login},
        {"display_name", displayName},
        {"password", password},
        {"captcha_token", captchaToken},
    });
}

NetworkClient::Result NetworkClient::login(const std::string& login,
                                            const std::string& password,
                                            const std::string& captchaToken) {
    return request({
        {"op", "login"},
        {"login", login},
        {"password", password},
        {"captcha_token", captchaToken},
    });
}

NetworkClient::Result NetworkClient::getCaptcha() {
    return request({{"op", "get_captcha"}});
}

NetworkClient::Result NetworkClient::fetchAccountState(const std::string& sessionToken) {
    return request({
        {"op", "account_state"},
        {"session_token", sessionToken},
    });
}

NetworkClient::Result NetworkClient::sendRemoteMessage(const std::string& sessionToken,
                                                        const std::string& toLogin,
                                                        const std::string& body) {
    return request({
        {"op", "send_message"},
        {"session_token", sessionToken},
        {"to_login", toLogin},
        {"body", body},
    });
}

NetworkClient::Result NetworkClient::pollMessages(const std::string& sessionToken) {
    return request({
        {"op", "poll_messages"},
        {"session_token", sessionToken},
    });
}

} // namespace msg
