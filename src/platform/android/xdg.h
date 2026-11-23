#pragma once
// Android config directory provider (replaces Linux xdg.h for cli.cpp)

#include <cstdlib>
#include <mutex>
#include <string>

namespace AndroidConfig {

inline std::mutex& Mutex() {
    static std::mutex m;
    return m;
}

inline std::string& DirStorage() {
    static std::string dir;
    return dir;
}

inline void SetConfigDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(Mutex());
    DirStorage() = dir;
    while (DirStorage().size() > 1 && DirStorage().back() == '/') {
        DirStorage().pop_back();
    }
}

inline std::string GetConfigDir() {
    std::lock_guard<std::mutex> lk(Mutex());
    if (!DirStorage().empty()) return DirStorage();
    const char* env = std::getenv("CLOUDREDIRECT_CONFIG_DIR");
    if (env && env[0]) return std::string(env);
    return "/data/local/tmp";
}

} // namespace AndroidConfig

inline std::string XdgConfigHome() {
    return AndroidConfig::GetConfigDir();
}
