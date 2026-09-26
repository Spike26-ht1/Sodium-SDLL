#include "Runtime.hpp"

#include <android/log.h>
#include <fstream>
#include <string>

#include <pl/Mod.hpp>
#include "render/FrameHook.hpp"

namespace sodium::core {
namespace {
constexpr const char* kLogTag = "SodiumSDLL";
constexpr int kMinMultiplier = 1;
constexpr int kMaxMultiplier = 3;
}

Runtime& Runtime::instance() {
    static Runtime runtime;
    return runtime;
}

bool Runtime::loadFrameSettings(const std::filesystem::path& configDir) {
    mConfigDir = configDir;
    std::error_code ec;
    std::filesystem::create_directories(mConfigDir, ec);

    const auto path = mConfigDir / "sdll.cfg";
    std::ifstream in(path);
    if (!in) {
        std::ofstream out(path);
        if (!out) return false;
        out << "# Sodium (SDLL)\n";
        out << "frame_multiplier=2\n";
        return true;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("frame_multiplier=", 0) != 0) continue;
        try {
            const int value = std::stoi(line.substr(17));
            if (value >= kMinMultiplier && value <= kMaxMultiplier) mFrameMultiplier = value;
        } catch (...) {
        }
    }
    return true;
}

bool Runtime::load(pl::mod::ModContext& context) {
    mLoaded = loadFrameSettings(context.configDir());
    if (!mLoaded) return false;

    render::FrameHook::instance().setMultiplier(mFrameMultiplier);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) loaded; frame multiplier=%d", mFrameMultiplier);
    return true;
}

bool Runtime::enable(pl::mod::ModContext&) {
    if (!mLoaded) return false;
    if (mEnabled) return true;

    render::FrameHook::instance().setMultiplier(mFrameMultiplier);
    if (!render::FrameHook::instance().install()) return false;

    mEnabled = true;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) enabled");
    return true;
}

bool Runtime::disable(pl::mod::ModContext&) {
    if (!mEnabled) return true;

    render::FrameHook::instance().uninstall();
    mEnabled = false;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) disabled");
    return true;
}

bool Runtime::unload(pl::mod::ModContext&) {
    if (mEnabled) return false;
    mLoaded = false;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) unloaded");
    return true;
}

}
