#include "Config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

using json = nlohmann::json;

namespace msg {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

bool Config::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return false;
    }

    source_path_ = path;

    if (j.contains("server")) {
        server_.host    = j["server"].value("host", server_.host);
        server_.port    = j["server"].value("port", server_.port);
        server_.use_tls = j["server"].value("use_tls", server_.use_tls);
    }
    if (j.contains("interface")) {
        interface_mode_ = j["interface"].value("mode", interface_mode_);
    }
    if (j.contains("limits")) {
        limits_.free_max_upload_gb    = j["limits"].value("free_max_upload_gb", limits_.free_max_upload_gb);
        limits_.premium_max_upload_gb = j["limits"].value("premium_max_upload_gb", limits_.premium_max_upload_gb);
    }
    return true;
}

bool Config::save() const {
    if (source_path_.empty()) return false;

    json j;
    j["server"]["host"]    = server_.host;
    j["server"]["port"]    = server_.port;
    j["server"]["use_tls"] = server_.use_tls;
    j["interface"]["mode"] = interface_mode_;
    j["limits"]["free_max_upload_gb"]    = limits_.free_max_upload_gb;
    j["limits"]["premium_max_upload_gb"] = limits_.premium_max_upload_gb;

    std::ofstream out(source_path_);
    if (!out.is_open()) return false;
    out << j.dump(2);
    return true;
}

std::filesystem::path Config::localDataDir() const {
    // Expand %APPDATA% by hand to keep this file dependency-free.
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? std::filesystem::path(appdata)
                                          : std::filesystem::temp_directory_path();
    return base / "MessengerClient" / "data";
}

} // namespace msg
