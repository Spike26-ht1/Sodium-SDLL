#pragma once

#include <filesystem>

namespace pl::mod { class ModContext; }

namespace sodium::core {

class Runtime {
public:
    static Runtime& instance();

    bool load(pl::mod::ModContext& context);
    bool enable(pl::mod::ModContext& context);
    bool disable(pl::mod::ModContext& context);
    bool unload(pl::mod::ModContext& context);

private:
    bool loadFrameSettings(const std::filesystem::path& configDir);

    bool mLoaded = false;
    bool mEnabled = false;
    int mFrameMultiplier = 2;
    std::filesystem::path mConfigDir;
};

}
