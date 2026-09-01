#include "Config.h"
#include "AppState.h"
#include "NetworkClient.h"
#include "Bridge.h"
#include "WebViewHost.h"
#include "Crypto.h"
#include "Branding.h"

#include <iostream>
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static std::string parseFlag(int argc, char** argv, const std::string& name, const std::string& def) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto pos = arg.find('=');
        if (pos != std::string::npos && arg.substr(0, pos) == name) {
            return arg.substr(pos + 1);
        }
    }
    return def;
}

static bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == name) return true;
    }
    return false;
}

static int handleCli(int argc, char** argv, msg::Bridge& bridge) {
    if (argc < 3) {
        std::cout << msg::branding::kAppNameA << " (" << msg::branding::kDeveloper << ")\n"
                     "Usage: messenger_client --cli <command> [--flag value ...]\n"
                     "Commands: captcha, login, register, whoami, logout\n"
                     "Example:\n"
                     "  messenger_client.exe --cli captcha\n"
                     "  messenger_client.exe --cli login --login alice --password pw --captchaToken <id>|<answer>\n";
        return 1;
    }
    std::string command = argv[2];
    nlohmann::json args = nlohmann::json::object();
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string key = argv[i];
        if (key.rfind("--", 0) == 0) key = key.substr(2);
        args[key] = argv[i + 1];
    }

    nlohmann::json result;
    if (command == "captcha")       result = bridge.dispatch("getCaptcha", args);
    else if (command == "login")        result = bridge.dispatch("login", args);
    else if (command == "register") result = bridge.dispatch("register", args);
    else if (command == "whoami")   result = bridge.dispatch("getSession", args);
    else if (command == "logout")   result = bridge.dispatch("logout", args);
    else {
        std::cerr << "Unknown CLI command: " << command << "\n";
        return 1;
    }

    std::cout << result.dump(2) << "\n";
    return result.value("ok", false) ? 0 : 1;
}

static int runMain(int argc, char** argv) {
    // Config lives next to the exe; current_path() is normally the exe's
    // own directory for a double-clicked WIN32 app, but fall back to the
    // module's actual directory if that ever isn't the case (e.g. launched
    // via a shortcut with a different "Start in" folder).
    auto configPath = std::filesystem::current_path() / "config" / "client.config.json";
#ifdef _WIN32
    if (!std::filesystem::exists(configPath)) {
        wchar_t modulePath[MAX_PATH];
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        auto exeDir = std::filesystem::path(modulePath).parent_path();
        std::filesystem::current_path(exeDir);
        configPath = exeDir / "config" / "client.config.json";
    }
#endif

    if (!msg::Config::instance().load(configPath)) {
        std::cerr << "[main] Could not load " << configPath
                  << " - falling back to built-in defaults (server=127.0.0.1).\n";
    }

    std::string serverOverride = parseFlag(argc, argv, "--server", "");
    if (!serverOverride.empty()) {
        auto colon = serverOverride.find(':');
        std::string host = colon == std::string::npos ? serverOverride : serverOverride.substr(0, colon);
        uint16_t port = colon == std::string::npos
            ? msg::Config::instance().server().port
            : static_cast<uint16_t>(std::stoi(serverOverride.substr(colon + 1)));
        msg::Config::instance().server().host = host;
        msg::Config::instance().server().port = port;
    }

    if (!msg::crypto::initLibrary()) {
        std::cerr << "[main] WARNING: crypto library not initialized.\n";
    }

    auto& srv = msg::Config::instance().server();
    msg::NetworkClient net(srv.host, srv.port, srv.use_tls);
    msg::Bridge bridge(net);

    if (hasFlag(argc, argv, "--cli")) {
        return handleCli(argc, argv, bridge);
    }

    // Requirement: works offline too, showing cached local data with a
    // small corner "no connection" indicator. The monitor never blocks
    // startup - the UI loads immediately either way.
    net.startConnectionMonitor(5);

    std::string mode = parseFlag(argc, argv, "--interface", msg::Config::instance().interfaceMode());
    msg::AppState::instance().setInterfaceMode(mode == "pro" ? msg::InterfaceMode::Pro : msg::InterfaceMode::Standard);
    std::string entry = (mode == "pro") ? "ui/pro/index.html" : "ui/standard/index.html";

    msg::WebViewHost host(bridge);
    bridge.onNavigateRequest = [&host](const std::string& entryHtmlRelativePath) {
        host.navigateTo(entryHtmlRelativePath);
    };

    if (!host.createAndShow(entry, 1200, 800)) {
        std::cerr << "[main] Failed to create UI window.\n";
        net.stopConnectionMonitor();
        return 1;
    }
    int rc = host.runMessageLoop();
    net.stopConnectionMonitor();
    return rc;
}

// Точка входа для Windows GUI (без консоли)
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

    char** argv = new char*[argc];
    for (int i = 0; i < argc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
        argv[i] = new char[len];
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, argv[i], len, nullptr, nullptr);
    }

    // --cli is meant to be run from an actual console. If launched that
    // way (cmd.exe / PowerShell) but built as a WIN32 subsystem app,
    // attach to the parent console so std::cout/cin actually reach it.
    if (hasFlag(argc, argv, "--cli")) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            freopen_s(&f, "CONIN$", "r", stdin);
        }
    }

    int result = runMain(argc, argv);

    for (int i = 0; i < argc; ++i) delete[] argv[i];
    delete[] argv;
    LocalFree(argvW);

    return result;
}
#else
int main(int argc, char** argv) {
    return runMain(argc, argv);
}
#endif
