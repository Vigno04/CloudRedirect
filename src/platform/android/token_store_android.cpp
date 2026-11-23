// Token storage for Android: private app-sandboxed 0600 file store.
// Android's internal filesDir is isolated per-UID; Platform().AtomicWriteText
// writes with 0600 permissions + fsync + atomic rename.

#include "cloud_provider_base.h"
#include "log.h"
#include "platform.h"

#include <filesystem>
#include <fstream>
#include <sstream>

class AndroidTokenStore : public ITokenStore {
public:
    std::string Read(const std::string& path) override {
        if (path.empty()) return {};
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) return {};
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    bool Write(const std::string& path, const std::string& json) override {
        if (path.empty()) return false;
        std::error_code ec;
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        return Platform().AtomicWriteText(path, json);
    }

    bool IsEncryptionAvailable() const override {
        // Relies on Android's per-app UID sandbox + 0600 file permissions.
        return false;
    }
};

std::unique_ptr<ITokenStore> CreateTokenStore() {
    return std::make_unique<AndroidTokenStore>();
}
