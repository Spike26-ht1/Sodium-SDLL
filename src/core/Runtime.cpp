#include "Runtime.hpp"

#include <android/log.h>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <cstring>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

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
            if (value >= kMinMultiplier && value <= kMaxMultiplier) {
                mFrameMultiplier = value;
            }
        } catch (...) {
        }
    }
    return true;
}

bool Runtime::tryInstallFrameHook() {
    if (!mEnabled) return false;
    if (render::FrameHook::instance().install()) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "Frame hook installed after runtime/library initialization");
        return true;
    }
    return false;
}

void* Runtime::dlopenDetour(const char* filename, int flags) {
    auto& runtime = Runtime::instance();
    const auto original = runtime.mOriginalDlopen;
    void* handle = original ? original(filename, flags) : nullptr;

    if (handle && filename && runtime.mEnabled &&
        std::strstr(filename, "libEGL.so") != nullptr) {
        runtime.tryInstallFrameHook();
    }

    return handle;
}

bool Runtime::installDlopenHook() {
    if (mDlopenHookInstalled) return true;

    void* libdl = dlopen("libdl.so", RTLD_NOW | RTLD_NOLOAD);
    if (!libdl) libdl = dlopen("libdl.so", RTLD_NOW);
    if (!libdl) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "libdl.so not available");
        return false;
    }

    void* target = dlsym(libdl, "dlopen");
    dlclose(libdl);

    if (!target) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "dlopen symbol not found");
        return false;
    }

    if (pl::memory::hook(
            target,
            reinterpret_cast<void*>(&Runtime::dlopenDetour),
            reinterpret_cast<void**>(&mOriginalDlopen)) != 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Failed to hook dlopen");
        mOriginalDlopen = nullptr;
        return false;
    }

    mDlopenHook.target = target;
    mDlopenHook.detour = reinterpret_cast<void*>(&Runtime::dlopenDetour);
    mDlopenHookInstalled = true;
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "dlopen watcher installed");
    return true;
}

void Runtime::removeDlopenHook() {
    if (!mDlopenHookInstalled) return;
    pl::memory::unhook(mDlopenHook.target, mDlopenHook.detour);
    mDlopenHook = {};
    mOriginalDlopen = nullptr;
    mDlopenHookInstalled = false;
}

bool Runtime::load(pl::mod::ModContext& context) {
    mLoaded = loadFrameSettings(context.configDir());
    if (!mLoaded) return false;

    render::FrameHook::instance().setMultiplier(mFrameMultiplier);

    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "Sodium (SDLL) loaded; frame multiplier=%d",
                        mFrameMultiplier);

    // EGL can be loaded after the Preloader mod lifecycle starts.
    // Install the watcher now, then retry as soon as libEGL is loaded.
    installDlopenHook();
    return true;
}

bool Runtime::enable(pl::mod::ModContext&) {
    if (!mLoaded) return false;
    if (mEnabled) return true;

    mEnabled = true;
    render::FrameHook::instance().setMultiplier(mFrameMultiplier);

    // Fast path: EGL may already be loaded.
    if (!tryInstallFrameHook()) {
        installDlopenHook();
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "Frame hook pending: waiting for libEGL.so");
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) enabled");
    return true;
}

bool Runtime::disable(pl::mod::ModContext&) {
    if (!mEnabled) return true;

    render::FrameHook::instance().uninstall();
    mEnabled = false;
    removeDlopenHook();

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) disabled");
    return true;
}

bool Runtime::unload(pl::mod::ModContext&) {
    if (mEnabled) return false;
    removeDlopenHook();
    mLoaded = false;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Sodium (SDLL) unloaded");
    return true;
}

}
