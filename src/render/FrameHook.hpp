#pragma once

#include <atomic>
#include <cstdint>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "core/HookState.hpp"

namespace sodium::render {

class FrameHook {
public:
    static FrameHook& instance();

    bool install();
    void uninstall();

    void setMultiplier(int multiplier);
    int multiplier() const { return mMultiplier.load(std::memory_order_relaxed); }
    std::uint64_t frameCount() const { return mFrameCount.load(std::memory_order_relaxed); }
    bool isInstalled() const { return mInstalled.load(std::memory_order_acquire); }

private:
    FrameHook() = default;
    FrameHook(const FrameHook&) = delete;
    FrameHook& operator=(const FrameHook&) = delete;

    static EGLBoolean swapBuffersDetour(EGLDisplay display, EGLSurface surface);
    EGLBoolean onSwapBuffers(EGLDisplay display, EGLSurface surface);

    using SwapBuffersFn = EGLBoolean(*)(EGLDisplay, EGLSurface);

    bool initResources();
    bool resizeResources(GLint width, GLint height);
    bool captureCurrentFrame(GLint width, GLint height);
    bool drawFrameTexture(float blend);
    void destroyResources();
    void restoreGlState(GLint previousProgram,
                        GLint previousFramebuffer,
                        GLint previousArrayBuffer,
                        GLint previousElementArrayBuffer,
                        GLint previousActiveTexture,
                        GLint previousTexture0,
                        GLint previousTexture1,
                        const GLint* previousViewport,
                        GLboolean previousScissor,
                        GLboolean previousDepth,
                        GLboolean previousBlend,
                        GLboolean previousCull,
                        GLboolean previousStencil);

    SwapBuffersFn mOriginal = nullptr;
    core::HookState mHook{};
    std::atomic<std::uint64_t> mFrameCount{0};
    std::atomic_bool mInstalled{false};
    std::atomic<int> mMultiplier{2};

    GLuint mProgram = 0;
    GLuint mVertexBuffer = 0;
    GLint mPositionLocation = -1;
    GLint mTexCoordLocation = -1;
    GLint mPreviousTextureLocation = -1;
    GLint mCurrentTextureLocation = -1;
    GLint mBlendLocation = -1;

    GLuint mPreviousTexture = 0;
    GLuint mCurrentTexture = 0;
    GLint mWidth = 0;
    GLint mHeight = 0;
    bool mHasPrevious = false;
    bool mResourcesInitialized = false;
};

}
