#include "Runtime.hpp"

#include <android/log.h>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

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

        if (!out)
            return false;

        out << "# Sodium (SDLL)\n";
        out << "frame_multiplier=2\n";
        return true;
    }

    std::string line;

    while (std::getline(in, line)) {
        if (line.rfind("frame_multiplier=", 0) != 0)
            continue;

        try {
            const int value = std::stoi(line.substr(17));

            if (value >= kMinMultiplier &&
                value <= kMaxMultiplier) {
                mFrameMultiplier = value;
            }
        } catch (...) {
        }
    }

    return true;
}

bool Runtime::tryInstallFrameHook() {
    if (!mEnabled)
        return false;

    std::lock_guard<std::mutex> lock(mHookMutex);

    if (render::FrameHook::instance().isInstalled())
        return true;

    if (render::FrameHook::instance().install()) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "eglSwapBuffers hook installed safely"
        );
        return true;
    }

    return false;
}

void Runtime::startEglWatcher() {
    if (mEglWatcher.joinable())
        return;

    mWatcherStop.store(
        false,
        std::memory_order_release
    );

    mEglWatcher = std::thread(
        &Runtime::eglWatcherLoop,
        this
    );

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "EGL watcher started"
    );
}

void Runtime::stopEglWatcher() {
    mWatcherStop.store(
        true,
        std::memory_order_release
    );

    if (mEglWatcher.joinable())
        mEglWatcher.join();
}

void Runtime::eglWatcherLoop() {
    using namespace std::chrono_literals;

    while (!mWatcherStop.load(
        std::memory_order_acquire
    )) {
        if (mEnabled) {
            void* egl = dlopen(
                "libEGL.so",
                RTLD_NOW | RTLD_NOLOAD
            );

            if (egl) {
                dlclose(egl);

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "libEGL.so detected"
                );

                if (tryInstallFrameHook()) {
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "EGL watcher finished"
                    );
                    return;
                }
            }
        }

        std::this_thread::sleep_for(100ms);
    }
}

bool Runtime::load(pl::mod::ModContext& context) {
    mLoaded = loadFrameSettings(
        context.configDir()
    );

    if (!mLoaded)
        return false;

    render::FrameHook::instance().setMultiplier(
        mFrameMultiplier
    );

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Sodium (SDLL) loaded; multiplier=%d",
        mFrameMultiplier
    );

    return true;
}

bool Runtime::enable(pl::mod::ModContext&) {
    if (!mLoaded)
        return false;

    if (mEnabled)
        return true;

    mEnabled = true;

    render::FrameHook::instance().setMultiplier(
        mFrameMultiplier
    );

    if (!tryInstallFrameHook()) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "EGL not ready; starting watcher"
        );

        startEglWatcher();
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Sodium (SDLL) enabled"
    );

    return true;
}

bool Runtime::disable(pl::mod::ModContext&) {
    if (!mEnabled)
        return true;

    mEnabled = false;

    stopEglWatcher();

    {
        std::lock_guard<std::mutex> lock(mHookMutex);
        render::FrameHook::instance().uninstall();
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Sodium (SDLL) disabled"
    );

    return true;
}

bool Runtime::unload(pl::mod::ModContext&) {
    if (mEnabled)
        return false;

    stopEglWatcher();

    mLoaded = false;

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Sodium (SDLL) unloaded"
    );

    return true;
}

}
