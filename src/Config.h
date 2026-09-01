#pragma once
#include <string>
#include <filesystem>

namespace msg {

// Loads config/client.config.json and exposes it as plain fields.
// Deliberately simple: the server address is the one thing that MUST be
// trivially changeable post-install (spec requirement #6), so it is never
// hard-coded anywhere else in the client - everything reads through here.
class Config {
public:
    struct Server {
        std::string host = "127.0.0.1";
        uint16_t    port = 8443;
        bool        use_tls = true;
    };

    struct Limits {
        long long free_max_upload_gb = 30;
        long long premium_max_upload_gb = -1; // -1 == unlimited
    };

    static Config& instance();

    bool load(const std::filesystem::path& path);
    bool save() const;

    Server& server() { return server_; }
    const Server& server() const { return server_; }

    Limits& limits() { return limits_; }

    std::string interfaceMode() const { return interface_mode_; } // "standard" | "pro"
    void setInterfaceMode(const std::string& mode) { interface_mode_ = mode; save(); }

    std::filesystem::path localDataDir() const;

private:
    Config() = default;
    std::filesystem::path source_path_;
    Server server_;
    Limits limits_;
    std::string interface_mode_ = "standard";
};

} // namespace msg
