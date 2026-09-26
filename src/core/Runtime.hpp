#pragma once

#include <filesystem>

#include "HookState.hpp"

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
    bool tryInstallFrameHook();
    bool installDlopenHook();
    void removeDlopenHook();

    static void* dlopenDetour(const char* filename, int flags);
    void* onDlopen(const char* filename, int flags);

    bool mLoaded = false;
    bool mEnabled = false;
    bool mDlopenHookInstalled = false;
    int mFrameMultiplier = 2;
    std::filesystem::path mConfigDir;

    using DlopenFn = void*(*)(const char*, int);
    DlopenFn mOriginalDlopen = nullptr;
    HookState mDlopenHook{};
};

}
